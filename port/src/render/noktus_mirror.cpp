#include "render/noktus_mirror.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "noktus/vulkan_renderer.hpp"
#include "platform/clock.hpp"
#include "render/noktus_adapter.hpp"
#include "render/passes.hpp"

namespace dc::render {

namespace {

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
    std::set<gfx::TextureHandle>                             wanted; // drawn with, and not made here: read across between ticks
    std::mutex                                               lock;
    bool                                                     open = false;
    std::uint64_t                                            drawn = 0;
    std::uint64_t                                            skipped = 0;
    std::uint64_t                                            elsewhere = 0;
    noktus::ObjectKey                                        key = noktus::ObjectKey::None;
    bool                                                     still = false;
    float                                                    jump_distance = std::numeric_limits<float>::infinity();
    bool                                                     vertices_move = false;
};

// A mirror is a test tool: whatever goes wrong in it, the game plays on.
bool g_failed = false;

template <typename Work>
void Guarded(Work work) {
    try {
        work();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "noktus mirror: stopped: %s\n", error.what());
        g_failed = true;
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

bool WindowRequested() {
    static const bool requested = [] {
        const char *setting = std::getenv("DC_NOKTUS_WINDOW");
        return setting != nullptr && std::string_view(setting) == "1";
    }();
    return requested;
}

void ReportPace(Mirror &mirror) {
    const noktus::PaceReport pace = noktus::TakePace(*mirror.renderer);
    std::fprintf(stderr, "noktus pacing: %llu frames, %llu ticks, mean %.2f ms, slowest 1%% %.2f ms, worst %.2f ms, held %llu, missed %llu, %d ahead, display timing %s, %s\n",
                 static_cast<unsigned long long>(pace.frames), static_cast<unsigned long long>(pace.ticks), pace.mean_interval, pace.slowest_1_percent, pace.worst_interval,
                 static_cast<unsigned long long>(pace.held), static_cast<unsigned long long>(pace.missed), pace.frames_ahead, pace.display_timing ? "yes" : "no", pace.variable ? "variable" : "fixed");
}

void WriteLastPicture() {
    Mirror &mirror = State();
    if (mirror.renderer) {
        Guarded([&] {
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
                mirror.renderer->DrawBetweenTicks(*moment);
                const std::vector<std::uint8_t> between = noktus::ReadScreen(*mirror.renderer, &width, &height);
                if (!between.empty()) {
                    gfx::WritePng(std::filesystem::path(mirror.path).replace_extension(".between.png"), between.data(), width, height);
                }
            }
        });
        std::fprintf(stderr, "noktus mirror: %llu drawn, %llu skipped for a texture it lacks, %llu to other targets, %llu dropped\n",
                     static_cast<unsigned long long>(mirror.drawn + mirror.meshes_drawn), static_cast<unsigned long long>(mirror.skipped),
                     static_cast<unsigned long long>(mirror.elsewhere), static_cast<unsigned long long>(mirror.renderer->DroppedDraws()));
        if (mirror.window != nullptr) {
            ReportPace(mirror);
        }
        mirror.renderer.reset();
        if (mirror.window != nullptr) {
            SDL_DestroyWindow(mirror.window);
            mirror.window = nullptr;
        }
    }
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
    if (WindowRequested()) {
        // Not focusable: the keyboard stays with the game's own window.
        mirror.window = SDL_CreateWindow("Noktus Nimbus", static_cast<int>(mapping.pixel_width), static_cast<int>(mapping.pixel_height), SDL_WINDOW_VULKAN | SDL_WINDOW_NOT_FOCUSABLE);
        if (mirror.window == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }
        std::uint32_t      count = 0;
        const char *const *names = SDL_Vulkan_GetInstanceExtensions(&count);
        host.instance_extensions.assign(names, names + count);
        host.create_surface = [window = mirror.window](void *instance, void **surface) {
            return SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(instance), nullptr, reinterpret_cast<VkSurfaceKHR *>(surface));
        };
        host.paced = true;
        host.tick_rate = ClockTickRate();
        if (const char *cushion = std::getenv("DC_NOKTUS_CUSHION")) {
            host.cushion = std::strtod(cushion, nullptr);
        }
        const char *interpolate = std::getenv("DC_NOKTUS_INTERPOLATE");
        host.interpolate = interpolate == nullptr || std::string_view(interpolate) != "0";
    }
    try {
        mirror.renderer = noktus::CreateVulkanRenderer(host);
    } catch (...) {
        if (mirror.window != nullptr) {
            SDL_DestroyWindow(mirror.window);
            mirror.window = nullptr;
        }
        throw;
    }
    mirror.textures[gfx::kPreviousFrame] = mirror.renderer->LastFrame();
    std::atexit(WriteLastPicture);
}

void ReadAcross(Mirror &mirror, gfx::TextureHandle handle) {
    std::vector<std::uint8_t> pixels;
    std::uint32_t             width = 0;
    std::uint32_t             height = 0;
    const auto                info = gfx::GetTextureInfo(handle);
    // A render target holds today's renderer's own drawing, which would make the mirror show that and prove nothing.
    if (!info || info->format != gfx::TextureFormat::Rgba8 || info->render_target || !gfx::ReadbackTexture(handle, pixels, width, height)) {
        return;
    }
    auto found = mirror.textures.find(handle);
    if (found == mirror.textures.end()) {
        noktus::TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.has_alpha = info->has_alpha;
        found = mirror.textures.emplace(handle, mirror.renderer->CreateTexture(desc)).first;
    }
    mirror.renderer->UpdateTexture(found->second, 0, {0, 0, width, height}, pixels);
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
        const auto found = mirror.textures.find(texture.texture);
        if (found == mirror.textures.end()) {
            mirror.wanted.insert(texture.texture);
            mirror.skipped++;
            return false;
        }
        scene.draw.material.albedo = found->second;
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

bool MirrorOn() {
    return (RequestedPath() != nullptr || WindowRequested()) && !g_failed;
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
        for (gfx::TextureHandle handle : mirror.wanted) {
            ReadAcross(mirror, handle);
        }
        // Reading a texture back stalls the GPU, which a window beside the game would show as a missed refresh at every tick. Once is enough for it, and a picture being compared needs them fresh.
        if (mirror.window != nullptr) {
            mirror.wanted.clear();
        }
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
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (mirror.renderer) {
        Guarded([&] { mirror.renderer->CutMotion(); });
    }
}

void MirrorCutCameraMotion() {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (mirror.renderer) {
        Guarded([&] { mirror.renderer->CutCameraMotion(); });
    }
}

void MirrorTexture(gfx::TextureHandle texture, const gfx::TextureDesc &desc) {
    if (!MirrorOn() || desc.format != gfx::TextureFormat::Rgba8) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
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
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    const auto       found = mirror.textures.find(texture);
    if (found != mirror.textures.end()) {
        const auto *bytes = static_cast<const std::uint8_t *>(pixels);
        Guarded([&] {
            mirror.renderer->UpdateTexture(found->second, level, {x, y, width, height}, std::span(bytes, static_cast<std::size_t>(width) * height * 4));
        });
    }
}

void MirrorDestroyTexture(gfx::TextureHandle texture) {
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    mirror.wanted.erase(texture);
    mirror.surfaces.erase(texture);
    const auto found = mirror.textures.find(texture);
    if (found != mirror.textures.end()) {
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

void MirrorCopyTexture(gfx::TextureHandle from, gfx::Rect rect, gfx::TextureHandle to, std::int32_t x, std::int32_t y) {
    Mirror           &mirror = State();
    std::scoped_lock  guard(mirror.lock);
    noktus::TextureId source = noktus::TextureId::None;
    noktus::TextureId target = noktus::TextureId::None;
    if (!mirror.renderer || !Known(mirror, from, &source) || !Known(mirror, to, &target)) {
        return;
    }
    const noktus::TexelRect texels = Texels(rect);
    Guarded([&] { mirror.renderer->Blit(source, texels, target, {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), texels.width, texels.height}, noktus::Filter::Nearest); });
}

void MirrorBlit(gfx::TextureHandle from, gfx::Rect from_rect, gfx::TextureHandle to, gfx::Rect to_rect, gfx::Filter filter) {
    Mirror           &mirror = State();
    std::scoped_lock  guard(mirror.lock);
    noktus::TextureId source = noktus::TextureId::None;
    noktus::TextureId target = noktus::TextureId::None;
    if (mirror.renderer && Known(mirror, from, &source) && Known(mirror, to, &target)) {
        Guarded([&] { mirror.renderer->Blit(source, Texels(from_rect), target, Texels(to_rect), filter == gfx::Filter::Nearest ? noktus::Filter::Nearest : noktus::Filter::Linear); });
    }
}

void MirrorSurface(gfx::TextureHandle target, std::uint32_t width, std::uint32_t height, bool has_alpha, bool shares_depth, bool frame_target) {
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

void MirrorCopyDepth(const gfx::LogicalRect &source) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (mirror.open && mirror.target_known) {
        Guarded([&] { mirror.renderer->CopySceneDepth({source.x, source.y, source.w, source.h}); });
    }
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
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    Guarded([&] {
        Start(mirror);
        mirror.meshes[mesh] = mirror.renderer->CreateMesh(Converted(vertices), indices);
    });
}

void MirrorUpdateMesh(gfx::MeshHandle mesh, std::uint32_t first, std::span<const gfx::Vertex3D> vertices) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    const auto       found = mirror.meshes.find(mesh);
    if (found != mirror.meshes.end()) {
        Guarded([&] { mirror.renderer->UpdateMesh(found->second, first, Converted(vertices)); });
    }
}

void MirrorDestroyMesh(gfx::MeshHandle mesh) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    const auto       found = mirror.meshes.find(mesh);
    if (found != mirror.meshes.end()) {
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
    Guarded([&] { mirror.renderer->Draw(Converted(vertices), indices, draw); });
    mirror.meshes_drawn++;
}

void MirrorDraw2D(gfx::Primitive primitive, std::span<const gfx::Vertex2D> vertices, const gfx::TextureBinding &texture,
                  const gfx::DrawState &state) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (!mirror.open) {
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
        const auto found = mirror.textures.find(texture.texture);
        if (found == mirror.textures.end()) {
            mirror.wanted.insert(texture.texture);
            mirror.skipped++;
            return;
        }
        draw.material.albedo = found->second;
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
    draw.layer = ToLayer(CurrentLayer2D());
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
    if (state.depth_test == gfx::DepthTest::Always && !state.depth_write) {
        draw.anchor.x = centre_x < anchor.left_until ? -1 : centre_x >= anchor.right_from ? 1 : 0;
        draw.anchor.y = centre_y < anchor.top_until ? -1 : centre_y >= anchor.bottom_from ? 1 : 0;
    }
    Guarded([&] { mirror.renderer->Draw(draw); });
    mirror.drawn++;
}

} // namespace dc::render
