#include "render/noktus_mirror.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "noktus/vulkan_renderer.hpp"
#include "platform/clock.hpp"
#include "platform/paths.hpp"
#include "render/noktus_adapter.hpp"
#include "render/passes.hpp"
#include "platform/load_work_profile.hpp"

namespace dc::render {

namespace {

// How many of Noktus's depth answers to look through for one of Kazarov's: the two answer a tick or so apart.
constexpr std::size_t kDepthAnswersKept = 4;
constexpr float       kDepthAgrees = 0.0005f;

// A paletted texture, kept as the game gave it. Noktus has no palettes: what it draws is one of these through a palette, made as it's first drawn that way, and made again when either
// changes. The game draws one image through another's palette, for text colours, so it's the pair that counts.
struct Indexed {
    std::uint32_t                          width = 0;
    std::uint32_t                          height = 0;
    bool                                   has_alpha = true;
    std::vector<std::vector<std::uint8_t>> levels;
    std::uint64_t                          version = 1;
};

struct Palette {
    std::array<std::uint32_t, 256> colors = {};
    std::uint64_t                  version = 1;
};

struct Expanded {
    noktus::TextureId id = noktus::TextureId::None;
    std::uint64_t     indexed_version = 0;
    std::uint64_t     palette_version = 0;
};

struct Mirror {
    std::string                                              path;
    SDL_Window                                              *window = nullptr;
    int                                                      ticks_since_report = 0;
    std::unique_ptr<noktus::Renderer>                        renderer;
    std::unordered_map<gfx::TextureHandle, noktus::TextureId> textures;
    std::unordered_map<gfx::MeshHandle, noktus::MeshId>       meshes;
    std::unordered_map<gfx::TextureHandle, noktus::SurfaceId> surfaces;
    bool                                                     target_known = true; // false: drawing to a target the mirror lacks
    std::uint64_t                                            meshes_drawn = 0;
    std::uint64_t                                            frames_at_rate = 0;
    std::chrono::steady_clock::time_point                    rate_since;
    double                                                   frames_per_second = 0.0;
    std::unordered_map<gfx::TextureHandle, Indexed>          indexed;
    std::unordered_map<gfx::TextureHandle, Palette>          palettes;
    std::map<std::pair<gfx::TextureHandle, gfx::TextureHandle>, Expanded> expanded;
    std::set<gfx::TextureHandle>                             frame_images; // hold a picture of the frame, so they lie where it does
    std::mutex                                               lock;
    bool                                                     open = false;
    std::uint64_t                                            drawn = 0;
    std::uint64_t                                            skipped = 0;
    std::uint64_t                                            elsewhere = 0;
    noktus::ObjectKey                                        key = noktus::ObjectKey::None;
    bool                                                     still = false;
    float                                                    jump_distance = std::numeric_limits<float>::infinity();
    bool                                                     vertices_move = false;
    std::uint64_t                                            draws_at_tick_end = 0;
    std::uint64_t                                            draws_last_tick = 0;
    HdrOutput                                                hdr;
    MirrorHdr                                                hdr_asked = MirrorHdr::Off;
    std::uint64_t                                            depth_compared = 0;
    float                                                    depth_difference = 0.0f; // the largest seen
    std::uint64_t                                            depth_differing = 0;
    std::uint64_t                                            depth_on_something = 0;  // not open sky: a read of nothing but sky would prove little
    std::array<std::array<float, kDepthAnswersKept>, noktus::Renderer::kDepthReads> depth_answers{}; // Noktus's latest, per slot, going round
    std::array<std::uint32_t, noktus::Renderer::kDepthReads>                       depth_answer_count{};
};

bool        g_failed = false;
bool        g_ready = false; // there's a window to draw on, or none is wanted
SDL_Window *g_game_window = nullptr;
MirrorSettings g_settings;
// For a swapchain that asks, on the render thread.
std::atomic<std::uint32_t> g_window_width = 0;
std::atomic<std::uint32_t> g_window_height = 0;

Mirror &State();

template <typename Work>
void Guarded(Work work) {
    try {
        work();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "noktus: stopped: %s\n", error.what());
        if (KazarovDraws()) {
            g_failed = true; // only a comparison stops: the game is still on screen
            State().open = false; // no more reaches a renderer that failed
            return;
        }
        if (g_game_window != nullptr) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Noktus Nimbus", error.what(), g_game_window);
        }
        std::_Exit(1); // nothing else can draw
    }
}

Mirror &State() {
    static Mirror mirror;
    return mirror;
}

const char *RequestedPath() {
    static const char *path = std::getenv("DC_NOKTUS_MIRROR");
    return path != nullptr && *path != '\0' ? path : nullptr;
}

bool InterpolationForcedOff() {
    const char *setting = std::getenv("DC_NOKTUS_INTERPOLATE");
    return setting != nullptr && std::string_view(setting) == "0";
}

void DropWindow(Mirror &mirror) {
    mirror.window = nullptr;
}

Pass FromNoktusPass(noktus::Pass pass);

void ReportPace(Mirror &mirror) {
    const noktus::PaceReport pace = noktus::TakePace(*mirror.renderer);
    std::fprintf(stderr, "noktus pacing: %llu frames, %llu ticks, mean %.2f ms, slowest 1%% %.2f ms, worst %.2f ms, held %llu, missed %llu, %d ahead, display timing %s, %s, target lead %.1f ms, work: frame %.3f ms, of it waiting %.3f ms, tick %.3f ms, link %.3f ms, %d workers, late pipelines %llu\n",
                 static_cast<unsigned long long>(pace.frames), static_cast<unsigned long long>(pace.ticks), pace.mean_interval, pace.slowest_1_percent, pace.worst_interval,
                 static_cast<unsigned long long>(pace.held), static_cast<unsigned long long>(pace.missed), pace.frames_ahead, pace.display_timing ? "yes" : "no", pace.variable ? "variable" : "fixed", pace.lead,
                 pace.mean_frame_work, pace.mean_frame_wait, pace.mean_tick_work, pace.mean_link_work, pace.workers, static_cast<unsigned long long>(pace.late_pipelines));
    const noktus::GpuTimes &gpu = pace.gpu;
    if (!gpu.measured) {
        std::fprintf(stderr, "noktus gpu: not timed, as this GPU can't time its work\n");
        return;
    }
    // Passes in the order the game draws them, so that one report reads against the next.
    std::string passes;
    for (std::size_t i = 0; i < gpu.mean_pass.size(); i++) {
        if (gpu.mean_pass[i] >= 0.0005) {
            passes += std::format(", {} {:.3f}", PassName(FromNoktusPass(static_cast<noktus::Pass>(i))), gpu.mean_pass[i]);
        }
    }
    std::fprintf(stderr, "noktus gpu: %llu frames, mean %.3f ms, worst %.3f ms; by pass%s, finishing %.3f\n", static_cast<unsigned long long>(gpu.frames), gpu.mean_frame,
                 gpu.worst_frame, passes.c_str(), gpu.mean_finish);
}

// With DC_NOKTUS_MOTION_OUT=<prefix>, the last tick drawn halfway, then at its end, and the second frame's motion: what the harness's motion check moves
// the first along to compare with the second. Pictures as PNG, motion (x then y, a pixel each) and reactive as raw 32-bit floats, their size in a JSON beside.
void WriteMotionPair(Mirror &mirror) {
    const char *prefix = std::getenv("DC_NOKTUS_MOTION_OUT");
    if (prefix == nullptr || *prefix == '\0') {
        return;
    }
    std::uint32_t                   width = 0;
    std::uint32_t                   height = 0;
    const std::vector<std::uint8_t> first = noktus::ReadMoment(*mirror.renderer, 0.5f, &width, &height);
    const std::vector<std::uint8_t> second = noktus::ReadMoment(*mirror.renderer, 1.0f, &width, &height);
    const noktus::MotionPicture     motion = noktus::ReadMotion(*mirror.renderer);
    if (first.empty() || second.empty() || motion.motion.empty()) {
        return;
    }
    const std::string base = prefix;
    gfx::WritePng(base + ".first.png", first.data(), width, height);
    gfx::WritePng(base + ".second.png", second.data(), width, height);
    std::ofstream(base + ".motion.f32", std::ios::binary).write(reinterpret_cast<const char *>(motion.motion.data()), static_cast<std::streamsize>(motion.motion.size() * sizeof(float)));
    std::ofstream(base + ".reactive.f32", std::ios::binary)
        .write(reinterpret_cast<const char *>(motion.reactive.data()), static_cast<std::streamsize>(motion.reactive.size() * sizeof(float)));
    std::ofstream(base + ".motion.json") << std::format(R"({{"width": {}, "height": {}, "continues": {}}})", motion.width, motion.height, motion.continues);
}

void WriteLastPicture() {
    Mirror &mirror = State();
    if (mirror.renderer) {
        Guarded([&] {
            WriteMotionPair(mirror);
            if (mirror.path.empty()) {
                return;
            }
            std::uint32_t                   width = 0;
            std::uint32_t                   height = 0;
            const std::vector<std::uint8_t> pixels = noktus::ReadScreen(*mirror.renderer, &width, &height);
            if (!pixels.empty()) {
                gfx::WritePng(mirror.path, pixels.data(), width, height);
            }
            if (const std::optional<float> moment = MirrorMoment()) {
                const std::vector<std::uint8_t> between = noktus::ReadMoment(*mirror.renderer, *moment, &width, &height);
                if (!between.empty()) {
                    gfx::WritePng(std::filesystem::path(mirror.path).replace_extension(".between.png"), between.data(), width, height);
                }
            }
        });
        std::fprintf(stderr, "noktus mirror: %llu drawn, %llu skipped for a texture it lacks, %llu to other targets, %llu dropped, %zu paletted pictures\n",
                     static_cast<unsigned long long>(mirror.drawn + mirror.meshes_drawn), static_cast<unsigned long long>(mirror.skipped),
                     static_cast<unsigned long long>(mirror.elsewhere), static_cast<unsigned long long>(mirror.renderer->DroppedDraws()), mirror.expanded.size());
        if (mirror.depth_compared != 0) {
            std::fprintf(stderr, "noktus depth: %llu reads compared, %llu on something, %llu differing, largest difference %.6f\n", static_cast<unsigned long long>(mirror.depth_compared),
                         static_cast<unsigned long long>(mirror.depth_on_something), static_cast<unsigned long long>(mirror.depth_differing), static_cast<double>(mirror.depth_difference));
        }
        if (mirror.window != nullptr) {
            ReportPace(mirror);
        }
        mirror.renderer.reset();
        DropWindow(mirror);
        // Nothing here means anything to a renderer started after this one.
        mirror.textures.clear();
        mirror.meshes.clear();
        mirror.surfaces.clear();
        mirror.indexed.clear();
        mirror.palettes.clear();
        mirror.expanded.clear();
        mirror.frame_images.clear();
        mirror.frames_at_rate = 0;
        mirror.frames_per_second = 0.0;
        mirror.rate_since = std::chrono::steady_clock::now();
    }
}

noktus::PresentMode ToPresentMode(gfx::PresentMode mode) {
    switch (mode) {
        case gfx::PresentMode::Mailbox:
            return noktus::PresentMode::Fast;
        case gfx::PresentMode::Immediate:
            return noktus::PresentMode::Off;
        case gfx::PresentMode::Fifo:
            break;
    }
    return noktus::PresentMode::VSync;
}

// Unnamed has no counterpart, since Noktus keeps the last pass named: a trace counts draws there, and there should be none.
// Switches with no default, so that -Wswitch warns of a pass added on either side without a counterpart.
std::optional<noktus::Pass> ToNoktusPass(Pass pass) {
    switch (pass) {
        case Pass::Unnamed:
        case Pass::Count:
            return std::nullopt;
        case Pass::TextureAnimation:
            return noktus::Pass::TextureAnimation;
        case Pass::Sky:
            return noktus::Pass::Sky;
        case Pass::Ground:
            return noktus::Pass::Ground;
        case Pass::Fishing:
            return noktus::Pass::Fishing;
        case Pass::Water:
            return noktus::Pass::Water;
        case Pass::DepthOfField:
            return noktus::Pass::DepthOfField;
        case Pass::Characters:
            return noktus::Pass::Characters;
        case Pass::Shadows:
            return noktus::Pass::Shadows;
        case Pass::MovingParts:
            return noktus::Pass::MovingParts;
        case Pass::Effects:
            return noktus::Pass::Effects;
        case Pass::LensFlare:
            return noktus::Pass::LensFlare;
        case Pass::Overlay:
            return noktus::Pass::Overlay;
        case Pass::Menu:
            return noktus::Pass::Menu;
        case Pass::Fade:
            return noktus::Pass::Fade;
        case Pass::Title:
            return noktus::Pass::Title;
        case Pass::Opening:
            return noktus::Pass::Opening;
        case Pass::Attract:
            return noktus::Pass::Attract;
        case Pass::Screen:
            return noktus::Pass::Screen;
    }
    return std::nullopt;
}

Pass FromNoktusPass(noktus::Pass pass) {
    switch (pass) {
        case noktus::Pass::TextureAnimation:
            return Pass::TextureAnimation;
        case noktus::Pass::Sky:
            return Pass::Sky;
        case noktus::Pass::Ground:
            return Pass::Ground;
        case noktus::Pass::Fishing:
            return Pass::Fishing;
        case noktus::Pass::Water:
            return Pass::Water;
        case noktus::Pass::DepthOfField:
            return Pass::DepthOfField;
        case noktus::Pass::Characters:
            return Pass::Characters;
        case noktus::Pass::Shadows:
            return Pass::Shadows;
        case noktus::Pass::MovingParts:
            return Pass::MovingParts;
        case noktus::Pass::Effects:
            return Pass::Effects;
        case noktus::Pass::LensFlare:
            return Pass::LensFlare;
        case noktus::Pass::Overlay:
            return Pass::Overlay;
        case noktus::Pass::Menu:
            return Pass::Menu;
        case noktus::Pass::Fade:
            return Pass::Fade;
        case noktus::Pass::Title:
            return Pass::Title;
        case noktus::Pass::Opening:
            return Pass::Opening;
        case noktus::Pass::Attract:
            return Pass::Attract;
        case noktus::Pass::Screen:
            return Pass::Screen;
        case noktus::Pass::Count:
            break;
    }
    return Pass::Unnamed;
}

const char *HdrName(MirrorHdr mode) {
    return mode == MirrorHdr::Hdr10 ? "hdr10" : mode == MirrorHdr::ScRgb ? "scrgb" : "off";
}

// Windows says how bright it shows SDR's white and how far past that the display goes, and those are what HDR starts from.
HdrOutput HdrForWindow(Mirror &mirror) {
    MirrorSettings settings = g_settings;
    if (const char *const asked = std::getenv("DC_NOKTUS_HDR")) {
        const std::string_view mode = asked;
        if (mode == "hdr10" || mode == "scrgb" || mode == "off") {
            settings.hdr = mode == "hdr10" ? MirrorHdr::Hdr10 : mode == "scrgb" ? MirrorHdr::ScRgb : MirrorHdr::Off;
        } else {
            static const bool warned = (std::fprintf(stderr, "DC_NOKTUS_HDR: %s isn't hdr10, scrgb or off, so the setting stands\n", asked), true);
            static_cast<void>(warned);
        }
    }
    const char *const force = std::getenv("DC_NOKTUS_HDR_FORCE");
    settings.force_hdr = force != nullptr && std::string_view(force) == "1";
    const SDL_PropertiesID properties = SDL_GetWindowProperties(mirror.window);
    const float            white = SDL_GetFloatProperty(properties, SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT, 1.0f);
    // SDL's flag means there's room past SDR's white, which a dim panel with SDR's brightness turned up doesn't have. Any white but 1 says HDR is on all the same.
    const bool      display_hdr = SDL_GetBooleanProperty(properties, SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false) || white != 1.0f;
    const HdrOutput output = ChooseHdr(settings, display_hdr, white, SDL_GetFloatProperty(properties, SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1.0f));
    if (output != mirror.hdr || settings.hdr != mirror.hdr_asked) {
        if (settings.hdr != MirrorHdr::Off && output.mode == MirrorHdr::Off) {
            std::fprintf(stderr, "noktus: %s asked for, but the display isn't in HDR, so it stays SDR\n", HdrName(settings.hdr));
        } else if (output.mode != MirrorHdr::Off) {
            std::fprintf(stderr, "noktus: %s asked for, the display %s in HDR, paper white %.0f nits, peak %.0f nits\n", HdrName(output.mode), display_hdr ? "is" : "isn't said to be", output.paper_white, output.peak);
        }
    }
    mirror.hdr = output;
    mirror.hdr_asked = settings.hdr;
    return output;
}

noktus::DisplayOutput ToDisplayOutput(MirrorHdr mode) {
    return mode == MirrorHdr::ScRgb ? noktus::DisplayOutput::ScRgb : mode == MirrorHdr::Hdr10 ? noktus::DisplayOutput::Hdr10 : noktus::DisplayOutput::Sdr;
}

void Start(Mirror &mirror) {
    if (mirror.renderer) {
        return;
    }
    mirror.path = RequestedPath() != nullptr ? RequestedPath() : "";
    const gfx::LogicalMapping mapping = gfx::GetLogicalMapping(gfx::kMainTarget);
    noktus::VulkanHost        host;
    host.pixel_size = [width = mapping.pixel_width, height = mapping.pixel_height](std::uint32_t *w, std::uint32_t *h) {
        *w = width;
        *h = height;
    };
    if (g_game_window != nullptr) {
        mirror.window = g_game_window;
        std::uint32_t      count = 0;
        const char *const *names = SDL_Vulkan_GetInstanceExtensions(&count);
        host.instance_extensions.assign(names, names + count);
        host.create_surface = [window = mirror.window](void *instance, void **surface) {
            return SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(instance), nullptr, reinterpret_cast<VkSurfaceKHR *>(surface));
        };
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(mirror.window, &width, &height);
        g_window_width.store(static_cast<std::uint32_t>(width));
        g_window_height.store(static_cast<std::uint32_t>(height));
        host.pixel_size = [](std::uint32_t *w, std::uint32_t *h) {
            *w = g_window_width.load();
            *h = g_window_height.load();
        };
        host.paced = true;
        host.tick_rate = ClockTickRate();
        const HdrOutput output = HdrForWindow(mirror);
        host.display_output = ToDisplayOutput(output.mode);
        host.paper_white = output.paper_white;
        host.peak_brightness = output.peak;
        if (const char *cushion = std::getenv("DC_NOKTUS_CUSHION")) {
            host.cushion = std::strtod(cushion, nullptr);
        }
    }
    if (const char *paint = std::getenv("DC_NOKTUS_PAINT_WINDOW")) {
        host.paint_window = std::string_view(paint) == "1";
    }
    if (const char *target = std::getenv("DC_NOKTUS_TARGET_TIMES")) {
        host.target_present_times = std::string_view(target) != "0";
    }
    host.frame_width = gfx::LogicalWidth();
    host.frame_height = gfx::LogicalHeight();
    host.interpolate = g_settings.interpolate && !InterpolationForcedOff();
    host.present_mode = ToPresentMode(g_settings.present_mode);
    host.frame_limit = g_settings.max_fps;
    host.anisotropy = g_settings.anisotropy;
    // Beside config.json, so that it goes wherever the player's settings do.
    host.pipeline_cache = PathsSaveRoot() / "noktus_pipelines.cache";
    if (const char *view = std::getenv("DC_NOKTUS_CLASS_VIEW")) {
        host.class_view = std::string_view(view) == "1";
    }
    if (const char *motion = std::getenv("DC_NOKTUS_MOTION")) {
        host.motion = std::string_view(motion) == "1";
    }
    if (const char *view = std::getenv("DC_NOKTUS_MOTION_VIEW"); view != nullptr && std::string_view(view) == "1") {
        host.motion = host.motion_view = true;
    }
    if (const char *workers = std::getenv("DC_NOKTUS_WORKERS")) {
        host.workers = static_cast<int>(std::strtol(workers, nullptr, 10));
    }
    if (const char *from = std::getenv("DC_NOKTUS_PARALLEL_FROM")) {
        host.parallel_from = std::strtoull(from, nullptr, 10);
    }
    try {
        mirror.renderer = noktus::CreateVulkanRenderer(host);
    } catch (...) {
        DropWindow(mirror);
        throw;
    }
    std::fprintf(stderr, "noktus: drawing on %s, window: %s\n", noktus::RendererDeviceName(*mirror.renderer).c_str(), noktus::DescribeWindow(*mirror.renderer).c_str());
    mirror.textures[gfx::kPreviousFrame] = mirror.renderer->LastFrame();
    if (const std::optional<noktus::Pass> pass = ToNoktusPass(CurrentPass())) {
        mirror.renderer->SetPass(*pass);
    }
    std::atexit(WriteLastPicture);
}

std::vector<noktus::Vertex3D> Converted(std::span<const gfx::Vertex3D> vertices) {
    std::vector<noktus::Vertex3D> out;
    out.reserve(vertices.size());
    for (const gfx::Vertex3D &vertex : vertices) {
        noktus::Vertex3D made;
        made.position = {vertex.position[0], vertex.position[1], vertex.position[2]};
        made.normal = {vertex.normal[0], vertex.normal[1], vertex.normal[2]};
        made.u = vertex.uv[0];
        made.v = vertex.uv[1];
        made.color = {static_cast<float>(vertex.color[0]) / 128.0f, static_cast<float>(vertex.color[1]) / 128.0f,
                      static_cast<float>(vertex.color[2]) / 128.0f, static_cast<float>(vertex.color[3]) / 128.0f};
        out.push_back(made);
    }
    return out;
}

std::optional<noktus::TextureId> Expand(Mirror &mirror, gfx::TextureHandle texture, gfx::TextureHandle palette) {
    const auto image = mirror.indexed.find(texture);
    const auto colors = mirror.palettes.find(palette);
    if (image == mirror.indexed.end() || colors == mirror.palettes.end()) {
        return std::nullopt;
    }
    Expanded &made = mirror.expanded[{texture, palette}];
    Guarded([&] {
        if (made.id == noktus::TextureId::None) {
            noktus::TextureDesc desc;
            desc.width = image->second.width;
            desc.height = image->second.height;
            desc.levels = static_cast<std::uint32_t>(image->second.levels.size());
            desc.has_alpha = image->second.has_alpha;
            made.id = mirror.renderer->CreateTexture(desc);
        }
        if (made.indexed_version == image->second.version && made.palette_version == colors->second.version) {
            return;
        }
        std::vector<std::uint32_t> pixels;
        for (std::size_t level = 0; level < image->second.levels.size(); level++) {
            const std::vector<std::uint8_t> &indices = image->second.levels[level];
            pixels.resize(indices.size());
            std::ranges::transform(indices, pixels.begin(), [&](std::uint8_t index) { return colors->second.colors[index]; });
            const std::uint32_t width = std::max(image->second.width >> level, 1u);
            const std::uint32_t height = std::max(image->second.height >> level, 1u);
            mirror.renderer->UpdateTexture(made.id, static_cast<std::uint32_t>(level), {0, 0, width, height}, std::span(reinterpret_cast<const std::uint8_t *>(pixels.data()), pixels.size() * 4));
        }
        made.indexed_version = image->second.version;
        made.palette_version = colors->second.version;
    });
    if (made.id == noktus::TextureId::None) {
        return std::nullopt;
    }
    return made.id;
}

// What Noktus knows a texture as, in the way this draw uses it.
std::optional<noktus::TextureId> Sampled(Mirror &mirror, const gfx::TextureBinding &binding) {
    const auto found = mirror.textures.find(binding.texture);
    if (found != mirror.textures.end()) {
        return found->second;
    }
    return Expand(mirror, binding.texture, binding.palette);
}

// A mesh draw as Noktus takes it, or false for one it can't take yet.
bool Prepare(Mirror &mirror, const gfx::MeshConstants &constants, const gfx::TextureBinding &texture, const gfx::DrawState &state,
             const gfx::MeshTransform *transform, noktus::MeshDraw &draw) {
    if (!mirror.open || transform == nullptr) {
        return false;
    }
    if (!mirror.target_known) {
        mirror.elsewhere++;
        return false;
    }
    MeshScene scene = ToMeshScene(constants, *transform, texture, state);
    if (texture.texture != gfx::kNullTexture) {
        const std::optional<noktus::TextureId> sampled = Sampled(mirror, texture);
        if (!sampled) {
            mirror.skipped++;
            return false;
        }
        scene.draw.material.albedo = *sampled;
    } else {
        scene.draw.material.albedo = noktus::TextureId::None;
    }
    mirror.renderer->SetCamera(scene.camera);
    draw = std::move(scene.draw);
    draw.key = mirror.key;
    draw.still = mirror.still;
    draw.jump_distance = mirror.jump_distance;
    draw.vertices_move = mirror.vertices_move;
    return true;
}

noktus::Primitive2D ToPrimitive(gfx::Primitive primitive) {
    switch (primitive) {
        case gfx::Primitive::Triangles:
            return noktus::Primitive2D::Triangles;
        case gfx::Primitive::TriangleStrip:
            return noktus::Primitive2D::TriangleStrip;
        case gfx::Primitive::TriangleFan:
            return noktus::Primitive2D::TriangleFan;
        case gfx::Primitive::Lines:
            return noktus::Primitive2D::Lines;
        case gfx::Primitive::LineStrip:
            return noktus::Primitive2D::LineStrip;
        case gfx::Primitive::Quads:
            break;
    }
    return noktus::Primitive2D::Quads;
}

noktus::Layer2D ToLayer(Layer2D layer) {
    switch (layer) {
        case Layer2D::World:
            return noktus::Layer2D::World;
        case Layer2D::Window:
            return noktus::Layer2D::Window;
        case Layer2D::Cover:
            return noktus::Layer2D::Cover;
        default:
            return noktus::Layer2D::Ui;
    }
}

} // namespace

HdrOutput ChooseHdr(const MirrorSettings &settings, bool display_hdr, float white, float headroom) {
    HdrOutput output;
    if (settings.hdr == MirrorHdr::Off || (!display_hdr && !settings.force_hdr)) {
        return output;
    }
    output.mode = settings.hdr;
    if (display_hdr) {
        output.paper_white = white * 80.0f;
        output.peak = output.paper_white * std::max(headroom, 1.0f);
    }
    if (settings.paper_white > 0.0f) {
        output.paper_white = settings.paper_white;
    }
    if (settings.peak > 0.0f) {
        output.peak = settings.peak;
    }
    output.paper_white = std::min(output.paper_white, output.peak);
    return output;
}

bool KazarovAsked(const char *setting) {
    return setting != nullptr && std::string_view(setting) == "kazarov";
}

bool KazarovDraws() {
    static const bool asked = [] {
        const char *const setting = std::getenv("DC_RENDERER");
        if (setting != nullptr && !KazarovAsked(setting) && std::string_view(setting) != "noktus") {
            std::fprintf(stderr, "DC_RENDERER: %s isn't a renderer, so Noktus draws: it takes noktus or kazarov\n", setting);
        }
        return KazarovAsked(setting);
    }();
    return asked;
}

bool MirrorOn() {
    return g_ready && !g_failed;
}

bool MirrorHasWindow() {
    return g_game_window != nullptr;
}

bool MirrorDepthProbe() {
    static const bool probe = [] {
        const char *setting = std::getenv("DC_NOKTUS_DEPTH_PROBE");
        return setting != nullptr && std::string_view(setting) == "1";
    }();
    return probe;
}

void MirrorApply(const MirrorSettings &settings) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    g_settings = settings;
    if (mirror.renderer && MirrorOn()) {
        noktus::SetInterpolation(*mirror.renderer, settings.interpolate && !InterpolationForcedOff());
        noktus::SetPresentMode(*mirror.renderer, ToPresentMode(settings.present_mode));
        noktus::SetFrameLimit(*mirror.renderer, settings.max_fps);
        noktus::SetAnisotropy(*mirror.renderer, settings.anisotropy);
        if (mirror.window != nullptr) {
            const HdrOutput output = HdrForWindow(mirror);
            noktus::SetDisplayOutput(*mirror.renderer, ToDisplayOutput(output.mode));
            noktus::SetHdrBrightness(*mirror.renderer, output.paper_white, output.peak);
        }
    }
}

void MirrorSetPass(Pass pass) {
    const std::optional<noktus::Pass> named = ToNoktusPass(pass);
    if (!named || !MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (mirror.renderer) {
        mirror.renderer->SetPass(*named);
    }
}

namespace {

noktus::Step ToNoktusStep(Step step) {
    switch (step) {
        case Step::Water:
            return noktus::Step::Water;
        case Step::DepthOfField:
            return noktus::Step::DepthOfField;
        case Step::Haze:
            return noktus::Step::Haze;
        case Step::DepthReads:
            return noktus::Step::DepthReads;
        case Step::Count:
            break;
    }
    return noktus::Step::Count;
}

Step FromNoktusStep(noktus::Step step) {
    switch (step) {
        case noktus::Step::Water:
            return Step::Water;
        case noktus::Step::DepthOfField:
            return Step::DepthOfField;
        case noktus::Step::Haze:
            return Step::Haze;
        case noktus::Step::DepthReads:
            return Step::DepthReads;
        case noktus::Step::Count:
            break;
    }
    return Step::Count;
}

} // namespace

void MirrorBeginStep(Step step) {
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (mirror.renderer) {
        mirror.renderer->BeginStep(ToNoktusStep(step));
    }
}

void MirrorEndStep() {
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (mirror.renderer) {
        mirror.renderer->EndStep();
    }
}

std::vector<MirrorStepReport> MirrorTickSteps() {
    std::vector<MirrorStepReport> steps;
    if (!MirrorOn()) {
        return steps;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (mirror.renderer) {
        for (const noktus::StepReport &report : noktus::TickSteps(*mirror.renderer)) {
            steps.push_back({.step = FromNoktusStep(report.step),
                             .commands = report.commands,
                             .reads_screen = report.reads_screen,
                             .reads_screen_depth = report.reads_screen_depth,
                             .writes_screen = report.writes_screen,
                             .writes_screen_depth = report.writes_screen_depth,
                             .textures_read = report.textures_read,
                             .textures_written = report.textures_written});
        }
    }
    return steps;
}

std::vector<MirrorClassTally> MirrorTakeClassTally() {
    std::vector<MirrorClassTally> tally;
    if (!MirrorOn()) {
        return tally;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (!mirror.renderer) {
        return tally;
    }
    for (const noktus::ClassTally &counted : noktus::TakeClassTally(*mirror.renderer)) {
        const char *name = "";
        switch (counted.draw_class) {
            case noktus::DrawClass::Opaque:
                name = "Opaque";
                break;
            case noktus::DrawClass::Cutout:
                name = "Cutout";
                break;
            case noktus::DrawClass::Transparent:
                name = "Transparent";
                break;
            case noktus::DrawClass::Decal:
                name = "Decal";
                break;
            case noktus::DrawClass::Effect:
                name = "Effect";
                break;
            case noktus::DrawClass::ShadowVolume:
                name = "ShadowVolume";
                break;
            case noktus::DrawClass::Screen:
                name = "Screen";
                break;
            case noktus::DrawClass::Interface:
                name = "Interface";
                break;
            case noktus::DrawClass::Count:
                break;
        }
        tally.push_back({.pass = FromNoktusPass(counted.pass), .draw_class = name, .rule = counted.rule, .draws = counted.draws});
    }
    return tally;
}

std::vector<MirrorPassStretch> MirrorTickPasses() {
    std::vector<MirrorPassStretch> stretches;
    if (!MirrorOn()) {
        return stretches;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (mirror.renderer) {
        for (const noktus::PassStretch &stretch : noktus::TickPasses(*mirror.renderer)) {
            stretches.push_back({.pass = FromNoktusPass(stretch.pass), .draws = stretch.draws, .copies = stretch.copies});
        }
    }
    return stretches;
}

HdrOutput MirrorHdrShown() {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    // What was asked for may have fallen back to SDR, where no patch shows.
    if (!mirror.renderer || !MirrorOn() || mirror.window == nullptr || noktus::ShownOutput(*mirror.renderer) == noktus::DisplayOutput::Sdr) {
        return {};
    }
    return mirror.hdr;
}

void MirrorHdrPatches(std::span<const MirrorHdrPatch> patches) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (!mirror.renderer || !MirrorOn()) {
        return;
    }
    std::vector<noktus::HdrPatch> given;
    for (const MirrorHdrPatch &patch : patches) {
        given.push_back({.x = patch.x, .y = patch.y, .width = patch.width, .height = patch.height, .nits = patch.nits});
    }
    noktus::SetHdrPatches(*mirror.renderer, given);
}

void MirrorTickRate(double ticks_per_second) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (mirror.renderer && MirrorOn()) {
        noktus::SetTickRate(*mirror.renderer, ticks_per_second);
    }
}

std::uint64_t MirrorDrawsLastTick() {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    return mirror.draws_last_tick;
}

void MirrorReadDepth(std::uint32_t slot, const gfx::LogicalRect &area) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (mirror.open) {
        Guarded([&] { mirror.renderer->ReadDepth(slot, {area.x, area.y, area.w, area.h}); });
    }
}

std::optional<float> MirrorDepthResult(std::uint32_t slot) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    return mirror.renderer ? mirror.renderer->DepthResult(slot) : std::nullopt;
}

void MirrorCompareDepth(std::uint32_t slot, float today) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (!mirror.renderer || slot >= noktus::Renderer::kDepthReads) {
        return;
    }
    const std::optional<float> answer = mirror.renderer->DepthResult(slot);
    if (!answer) {
        return;
    }
    std::array<float, kDepthAnswersKept> &kept = mirror.depth_answers[slot];
    std::uint32_t                        &count = mirror.depth_answer_count[slot];
    kept[count++ % kDepthAnswersKept] = *answer;
    if (count >= kDepthAnswersKept) {
        float nearest = std::numeric_limits<float>::infinity();
        for (const float value : kept) {
            nearest = std::min(nearest, std::abs(today - value));
        }
        mirror.depth_compared++;
        mirror.depth_on_something += today > 0.0f ? 1 : 0;
        mirror.depth_differing += nearest > kDepthAgrees ? 1 : 0;
        mirror.depth_difference = std::max(mirror.depth_difference, nearest);
    }
}

double MirrorFramesPerSecond() {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    return mirror.frames_per_second;
}

void MirrorTakeWindow(SDL_Window *window) {
    g_game_window = KazarovDraws() ? nullptr : window;
    g_ready = !KazarovDraws() || RequestedPath() != nullptr;
}

void MirrorDropWindow() {
    WriteLastPicture();
    g_game_window = nullptr;
    g_ready = false;
}

std::string MirrorDeviceName() {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    std::string      name;
    if (MirrorOn()) {
        Guarded([&] {
            Start(mirror);
            name = noktus::RendererDeviceName(*mirror.renderer);
        });
    }
    return name;
}

bool MirrorReadScreen(std::vector<std::uint8_t> &pixels, std::uint32_t &width, std::uint32_t &height) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (!mirror.renderer) {
        return false;
    }
    // With a window, the last picture is whatever frame came last: asking for the tick's end gives the same picture each run.
    Guarded([&] { pixels = mirror.window != nullptr ? noktus::ReadMoment(*mirror.renderer, 1.0f, &width, &height) : noktus::ReadScreen(*mirror.renderer, &width, &height); });
    return !pixels.empty();
}

void MirrorStop() {
    WriteLastPicture();
    g_failed = true; // it's finished, and nothing is to start it again
}

std::optional<float> MirrorMoment() {
    static const char *moment = std::getenv("DC_NOKTUS_MOMENT");
    if (moment == nullptr || *moment == '\0') {
        return std::nullopt;
    }
    return std::strtof(moment, nullptr);
}

void MirrorBeginTick() {
    if (!MirrorOn()) {
        return;
    }
    Mirror               &mirror = State();
    std::scoped_lock      guard(mirror.lock);
    Guarded([&] {
        Start(mirror);
        // What's being drawn to: the window, or with none the size the run asked for, which beside Kazarov's renderer is what it's drawing.
        const gfx::LogicalMapping main = gfx::GetLogicalMapping(gfx::kMainTarget);
        int                       width = static_cast<int>(main.pixel_width);
        int                       height = static_cast<int>(main.pixel_height);
        if (mirror.window != nullptr) {
            SDL_GetWindowSizeInPixels(mirror.window, &width, &height);
            g_window_width.store(static_cast<std::uint32_t>(width));
            g_window_height.store(static_cast<std::uint32_t>(height));
        }
        mirror.renderer->Resize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
        const gfx::FrameLayout layout = gfx::CurrentFrameLayout();
        mirror.renderer->SetLayout({.fit = layout.aspect == gfx::AspectMode::Fill ? noktus::FrameFit::Fill : noktus::FrameFit::Letterbox, .interface_scale = layout.ui_scale});
        mirror.renderer->BeginTick();
        mirror.open = true;
        mirror.target_known = true;
    });
}

void MirrorEndTick() {
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (mirror.open) {
        Guarded([&] { mirror.renderer->EndTick(); });
        mirror.open = false;
        mirror.draws_last_tick = mirror.drawn + mirror.meshes_drawn - mirror.draws_at_tick_end;
        mirror.draws_at_tick_end = mirror.drawn + mirror.meshes_drawn;
        if (mirror.window != nullptr) {
            const auto now = std::chrono::steady_clock::now();
            const double seconds = std::chrono::duration<double>(now - mirror.rate_since).count();
            if (seconds >= 0.5) {
                const std::uint64_t frames = noktus::FramesShown(*mirror.renderer);
                mirror.frames_per_second = static_cast<double>(frames - mirror.frames_at_rate) / seconds;
                mirror.frames_at_rate = frames;
                mirror.rate_since = now;
            }
        }
        // Every few seconds, so that a load doesn't hide how play went.
        if (mirror.window != nullptr && ++mirror.ticks_since_report == 300) {
            mirror.ticks_since_report = 0;
            ReportPace(mirror);
        }
    }
}

void MirrorMotion(gfx::InterpKey key, bool no_interpolation, float teleport_distance, bool blend_vertices) {
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    mirror.key = static_cast<noktus::ObjectKey>(key);
    mirror.still = no_interpolation;
    mirror.jump_distance = teleport_distance;
    mirror.vertices_move = blend_vertices;
}

void MirrorCutMotion() {
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (mirror.renderer) {
        Guarded([&] { mirror.renderer->CutMotion(); });
    }
}

void MirrorCutCameraMotion() {
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (mirror.renderer) {
        Guarded([&] { mirror.renderer->CutCameraMotion(); });
    }
}

void MirrorTexture(gfx::TextureHandle texture, const gfx::TextureDesc &desc) {
    const platform::load_profile::Span profile(platform::load_profile::Kind::GpuObjects);
    if (!MirrorOn() || texture == gfx::kNullTexture || (desc.format != gfx::TextureFormat::Rgba8 && desc.format != gfx::TextureFormat::Index8)) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (desc.format == gfx::TextureFormat::Index8) {
        Indexed &image = mirror.indexed[texture];
        image = {.width = desc.width, .height = desc.height, .has_alpha = desc.has_alpha};
        image.levels.resize(std::max(desc.mip_levels, 1u));
        for (std::size_t level = 0; level < image.levels.size(); level++) {
            image.levels[level].resize(static_cast<std::size_t>(std::max(desc.width >> level, 1u)) * std::max(desc.height >> level, 1u));
        }
        return;
    }
    Guarded([&] {
        Start(mirror);
        noktus::TextureDesc made;
        made.width = desc.width;
        made.height = desc.height;
        made.levels = desc.mip_levels;
        made.has_alpha = desc.has_alpha;
        mirror.textures[texture] = mirror.renderer->CreateTexture(made);
    });
}

void MirrorTexturePixels(gfx::TextureHandle texture, std::uint32_t level, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                         std::uint32_t height, const void *pixels) {
    const platform::load_profile::Span profile(platform::load_profile::Kind::GpuObjects);
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (const auto image = mirror.indexed.find(texture); image != mirror.indexed.end() && level < image->second.levels.size()) {
        const auto         *indices = static_cast<const std::uint8_t *>(pixels);
        const std::uint32_t row = std::max(image->second.width >> level, 1u);
        const std::uint32_t rows = std::max(image->second.height >> level, 1u);
        if (x > row || width > row - x || y > rows || height > rows - y) {
            return;
        }
        for (std::uint32_t line = 0; line < height; line++) {
            std::memcpy(image->second.levels[level].data() + static_cast<std::size_t>(y + line) * row + x, indices + static_cast<std::size_t>(line) * width, width);
        }
        image->second.version++;
        return;
    }
    const auto       found = mirror.textures.find(texture);
    if (found != mirror.textures.end()) {
        const auto *bytes = static_cast<const std::uint8_t *>(pixels);
        Guarded([&] {
            mirror.renderer->UpdateTexture(found->second, level, {x, y, width, height}, std::span(bytes, static_cast<std::size_t>(width) * height * 4));
        });
    }
}

void MirrorPalette(gfx::TextureHandle palette, const std::uint32_t *colors, std::uint32_t first, std::uint32_t count) {
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (first > std::tuple_size_v<decltype(Palette::colors)> || count > std::tuple_size_v<decltype(Palette::colors)> - first) {
        return;
    }
    Palette &kept = mirror.palettes[palette];
    // Text sets its palette on every pass, and a colour rarely changes: making every picture drawn through it again would cost far more than looking.
    if (!std::equal(colors, colors + count, kept.colors.begin() + first)) {
        std::copy_n(colors, count, kept.colors.begin() + first);
        kept.version++;
    }
}

void MirrorDestroyTexture(gfx::TextureHandle texture) {
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    mirror.surfaces.erase(texture);
    mirror.frame_images.erase(texture);
    if (mirror.indexed.erase(texture) + mirror.palettes.erase(texture) != 0) {
        for (auto made = mirror.expanded.begin(); made != mirror.expanded.end();) {
            if (made->first.first != texture && made->first.second != texture) {
                ++made;
                continue;
            }
            if (mirror.renderer && made->second.id != noktus::TextureId::None) {
                Guarded([&] { mirror.renderer->DestroyTexture(made->second.id); });
            }
            made = mirror.expanded.erase(made);
        }
    }
    const auto found = mirror.textures.find(texture);
    if (found != mirror.textures.end() && mirror.renderer) {
        mirror.renderer->DestroyTexture(found->second);
        mirror.textures.erase(found);
    }
}

namespace {

// A texture as Noktus knows it, or false. The screen is TextureId::None.
bool Known(Mirror &mirror, gfx::TextureHandle handle, noktus::TextureId *id) {
    if (handle == gfx::kMainTarget) {
        *id = noktus::TextureId::None;
        return true;
    }
    const auto found = mirror.textures.find(handle);
    if (found == mirror.textures.end()) {
        return false;
    }
    *id = found->second;
    return true;
}

noktus::TexelRect Texels(gfx::Rect rect) {
    return {static_cast<std::uint32_t>(std::max(rect.x, 0)), static_cast<std::uint32_t>(std::max(rect.y, 0)), static_cast<std::uint32_t>(std::max(rect.w, 0)),
            static_cast<std::uint32_t>(std::max(rect.h, 0))};
}

} // namespace

namespace {

// A copy between paletted textures, on what's kept of them. Texture animations move frames along a sheet this way, so a copy within one texture reads what it held before.
bool CopyIndexed(Mirror &mirror, gfx::TextureHandle from, const gfx::Rect &from_rect, gfx::TextureHandle to, const gfx::Rect &to_rect) {
    const auto source = mirror.indexed.find(from);
    const auto target = mirror.indexed.find(to);
    if (source == mirror.indexed.end() || target == mirror.indexed.end() || from_rect.w <= 0 || from_rect.h <= 0 || to_rect.w <= 0 || to_rect.h <= 0) {
        return false;
    }
    std::vector<std::uint8_t>       held;
    if (from == to) {
        held = source->second.levels[0];
    }
    const std::vector<std::uint8_t> &before = from == to ? held : source->second.levels[0];
    const auto                      source_width = static_cast<std::int64_t>(source->second.width);
    const auto                      source_height = static_cast<std::int64_t>(source->second.height);
    const auto                      target_width = static_cast<std::int64_t>(target->second.width);
    const auto                      target_height = static_cast<std::int64_t>(target->second.height);
    std::vector<std::uint8_t>      &after = target->second.levels[0];
    bool                            changed = false;
    for (std::int64_t y = 0; y < to_rect.h; y++) {
        const std::int64_t to_y = to_rect.y + y;
        const std::int64_t from_y = from_rect.y + y * from_rect.h / to_rect.h;
        if (to_y < 0 || to_y >= target_height || from_y < 0 || from_y >= source_height) {
            continue;
        }
        for (std::int64_t x = 0; x < to_rect.w; x++) {
            const std::int64_t to_x = to_rect.x + x;
            const std::int64_t from_x = from_rect.x + x * from_rect.w / to_rect.w;
            if (to_x >= 0 && to_x < target_width && from_x >= 0 && from_x < source_width) {
                std::uint8_t      &texel = after[static_cast<std::size_t>(to_y * target_width + to_x)];
                const std::uint8_t wanted = before[static_cast<std::size_t>(from_y * source_width + from_x)];
                changed = changed || texel != wanted;
                texel = wanted;
            }
        }
    }
    // As with palettes: a copy that changes nothing shouldn't make every picture of this texture again.
    if (changed) {
        target->second.version++;
    }
    return true;
}

} // namespace

void MirrorCopyTexture(gfx::TextureHandle from, gfx::Rect rect, gfx::TextureHandle to, std::int32_t x, std::int32_t y) {
    if (!MirrorOn()) {
        return;
    }
    Mirror           &mirror = State();
    std::scoped_lock  guard(mirror.lock);
    if (CopyIndexed(mirror, from, rect, to, {x, y, rect.w, rect.h})) {
        return;
    }
    noktus::TextureId source = noktus::TextureId::None;
    noktus::TextureId target = noktus::TextureId::None;
    if (!mirror.renderer || !Known(mirror, from, &source) || !Known(mirror, to, &target)) {
        return;
    }
    const noktus::TexelRect texels = Texels(rect);
    Guarded([&] { mirror.renderer->Blit(source, texels, target, {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), texels.width, texels.height}, noktus::Filter::Nearest); });
}

void MirrorBlit(gfx::TextureHandle from, gfx::Rect from_rect, gfx::TextureHandle to, gfx::Rect to_rect, gfx::Filter filter) {
    if (!MirrorOn()) {
        return;
    }
    Mirror           &mirror = State();
    std::scoped_lock  guard(mirror.lock);
    if (CopyIndexed(mirror, from, from_rect, to, to_rect)) {
        return;
    }
    noktus::TextureId source = noktus::TextureId::None;
    noktus::TextureId target = noktus::TextureId::None;
    if (mirror.renderer && Known(mirror, from, &source) && Known(mirror, to, &target)) {
        Guarded([&] { mirror.renderer->Blit(source, Texels(from_rect), target, Texels(to_rect), filter == gfx::Filter::Nearest ? noktus::Filter::Nearest : noktus::Filter::Linear); });
    }
}

void MirrorSurface(gfx::TextureHandle target, std::uint32_t width, std::uint32_t height, bool has_alpha, bool shares_depth, bool frame_target) {
    const platform::load_profile::Span profile(platform::load_profile::Kind::GpuObjects);
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (target == gfx::kNullTexture || mirror.surfaces.contains(target)) {
        return;
    }
    Guarded([&] {
        Start(mirror);
        noktus::SurfaceDesc desc;
        desc.width = width;
        desc.height = height;
        desc.has_alpha = has_alpha;
        desc.shares_scene_depth = shares_depth;
        desc.follows_window = frame_target;
        if (shares_depth || frame_target) {
            mirror.frame_images.insert(target);
        }
        const noktus::SurfaceId surface = mirror.renderer->Surface(desc);
        mirror.surfaces[target] = surface;
        mirror.textures[target] = mirror.renderer->SurfaceTexture(surface);
    });
}

void MirrorSetTarget(gfx::TextureHandle target) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (!mirror.open) {
        return;
    }
    const auto found = mirror.surfaces.find(target);
    mirror.target_known = target == gfx::kMainTarget || found != mirror.surfaces.end();
    if (mirror.target_known) {
        Guarded([&] { mirror.renderer->DrawTo(target == gfx::kMainTarget ? noktus::SurfaceId::None : found->second); });
    }
}

void MirrorSnapshotFrame(gfx::TextureHandle target) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    const auto       found = mirror.surfaces.find(target);
    if (mirror.open && found != mirror.surfaces.end()) {
        Guarded([&] { mirror.renderer->GrabScreen(found->second); });
    }
}

bool MirrorCopyDepth(const gfx::LogicalRect &source) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    bool             copied = false;
    if (mirror.open && mirror.target_known) {
        Guarded([&] { copied = mirror.renderer->CopySceneDepth({source.x, source.y, source.w, source.h}); });
    }
    return copied;
}

void MirrorClear(bool color, const std::uint8_t *rgba, bool depth, float depth_value, const gfx::LogicalRect *rect) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (!mirror.open || !mirror.target_known) {
        return;
    }
    noktus::Color clear = {0.0f, 0.0f, 0.0f, 0.0f};
    if (color && rgba != nullptr) {
        clear = {static_cast<float>(rgba[0]) / 255.0f, static_cast<float>(rgba[1]) / 255.0f, static_cast<float>(rgba[2]) / 255.0f, std::min(static_cast<float>(rgba[3]) / 128.0f, 1.0f)};
    }
    std::optional<noktus::Rect> area;
    if (rect != nullptr) {
        area = noktus::Rect{rect->x, rect->y, rect->w, rect->h};
    }
    Guarded([&] { mirror.renderer->Clear(clear, color, depth_value, depth, area); });
}

void MirrorClearStencil() {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (mirror.open && mirror.target_known) {
        Guarded([&] { mirror.renderer->ClearShadowVolumes(); });
    }
}

void MirrorCreateMesh(gfx::MeshHandle mesh, std::span<const gfx::Vertex3D> vertices, std::span<const std::uint32_t> indices) {
    const platform::load_profile::Span profile(platform::load_profile::Kind::GpuObjects);
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    Guarded([&] {
        Start(mirror);
        mirror.meshes[mesh] = mirror.renderer->CreateMesh(Converted(vertices), indices);
    });
}

void MirrorUpdateMesh(gfx::MeshHandle mesh, std::uint32_t first, std::span<const gfx::Vertex3D> vertices) {
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    const auto       found = mirror.meshes.find(mesh);
    if (found != mirror.meshes.end() && mirror.renderer) {
        Guarded([&] { mirror.renderer->UpdateMesh(found->second, first, Converted(vertices)); });
    }
}

void MirrorDestroyMesh(gfx::MeshHandle mesh) {
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    const auto       found = mirror.meshes.find(mesh);
    if (found != mirror.meshes.end() && mirror.renderer) {
        Guarded([&] { mirror.renderer->DestroyMesh(found->second); });
        mirror.meshes.erase(found);
    }
}

void MirrorDrawMesh(gfx::MeshHandle mesh, std::uint32_t first_index, std::uint32_t index_count, const gfx::MeshConstants &constants,
                    const gfx::TextureBinding &texture, const gfx::DrawState &state, const gfx::MeshTransform *transform) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    const auto       found = mirror.meshes.find(mesh);
    noktus::MeshDraw draw;
    if (found == mirror.meshes.end() || !Prepare(mirror, constants, texture, state, transform, draw)) {
        return;
    }
    draw.mesh = found->second;
    draw.first_index = first_index;
    draw.index_count = index_count;
    Guarded([&] { mirror.renderer->Draw(draw); });
    mirror.meshes_drawn++;
}

void MirrorDrawImmediate(std::span<const gfx::Vertex3D> vertices, std::span<const std::uint32_t> indices, const gfx::MeshConstants &constants,
                         const gfx::TextureBinding &texture, const gfx::DrawState &state, const gfx::MeshTransform *transform) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    noktus::MeshDraw draw;
    if (!Prepare(mirror, constants, texture, state, transform, draw)) {
        return;
    }
    draw.index_count = static_cast<std::uint32_t>(indices.size());
    // A shadow volume is put together for this tick's camera, pose and light, with whatever triangles that takes. Other flat shadows reach here too, if any are ever immediate: moving with
    // their object is right for those as well.
    draw.rebuilt = (constants.flags & gfx::kMeshShadow) != 0;
    Guarded([&] { mirror.renderer->Draw(Converted(vertices), indices, draw); });
    mirror.meshes_drawn++;
}

void MirrorDraw2D(gfx::Primitive primitive, std::span<const gfx::Vertex2D> vertices, const gfx::TextureBinding &texture,
                  const gfx::DrawState &state, bool over_the_window) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (!mirror.open || vertices.empty()) {
        return;
    }
    if (!mirror.target_known) {
        mirror.elsewhere++;
        return;
    }
    const bool textured = texture.texture != gfx::kNullTexture;
    noktus::Draw2D draw;
    draw.material = ToMaterial(state, texture);
    if (textured) {
        const std::optional<noktus::TextureId> sampled = Sampled(mirror, texture);
        if (!sampled) {
            mirror.skipped++;
            return;
        }
        draw.material.albedo = *sampled;
    } else {
        draw.material.albedo = noktus::TextureId::None;
    }
    std::vector<noktus::Vertex2D> converted;
    converted.reserve(vertices.size());
    for (const gfx::Vertex2D &vertex : vertices) {
        converted.push_back(ToVertex2D(vertex, textured));
    }
    draw.vertices = converted;
    draw.primitive = ToPrimitive(primitive);
    draw.layer = ToLayer(DrawLayer2D(primitive, vertices, texture, state));
    draw.over_the_window = over_the_window;
    // Whole draws anchor by the centre of their bounds, as today's renderer decides it.
    const gfx::UiAnchor anchor = gfx::CurrentUiAnchor();
    float               low_x = vertices[0].x, high_x = vertices[0].x, low_y = vertices[0].y, high_y = vertices[0].y;
    for (const gfx::Vertex2D &vertex : vertices) {
        low_x = std::min(low_x, vertex.x);
        high_x = std::max(high_x, vertex.x);
        low_y = std::min(low_y, vertex.y);
        high_y = std::max(high_y, vertex.y);
    }
    const float centre_x = (low_x + high_x) * 0.5f;
    const float centre_y = (low_y + high_y) * 0.5f;
    // Depth places a draw among the meshes, and a picture of the frame lies where the frame does.
    draw.follows_interface_size = state.depth_test == gfx::DepthTest::Always && !state.depth_write && texture.texture != gfx::kPreviousFrame && !mirror.frame_images.contains(texture.texture);
    if (draw.follows_interface_size) {
        draw.anchor.x = centre_x < anchor.left_until ? -1 : centre_x >= anchor.right_from ? 1 : 0;
        draw.anchor.y = centre_y < anchor.top_until ? -1 : centre_y >= anchor.bottom_from ? 1 : 0;
        draw.anchor.max_aspect = anchor.max_aspect;
        draw.anchor.moves_clip = anchor.scissor && (draw.anchor.x != 0 || draw.anchor.y != 0);
    }
    Guarded([&] { mirror.renderer->Draw(draw); });
    mirror.drawn++;
}

} // namespace dc::render
