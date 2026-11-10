#include "render/passes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "render/noktus_adapter.hpp"

#if defined(DC_PASS_STACKS)
#include <dlfcn.h>
#include <execinfo.h>

#include <format>
#endif

namespace dc::render {

namespace {

constexpr std::array<const char *, static_cast<std::size_t>(Pass::Count)> kNames = {
    "Unnamed", "TextureAnimation", "Sky",     "Ground",    "Fishing", "Water", "DepthOfField", "Characters", "Shadows", "MovingParts",
    "Effects", "LensFlare", "Overlay", "Menu",    "Fade",    "Title", "Opening",      "Attract",    "Screen",
};

struct Span {
    Pass          pass = Pass::Unnamed;
    std::uint32_t draws = 0;
    std::uint32_t copies = 0;
};

// One shape of frame: the same passes in the same order.
struct Shape {
    std::uint64_t              ticks = 0;
    std::vector<std::uint64_t> draws;
    std::vector<std::uint64_t> copies;
};

constexpr std::array<const char *, static_cast<std::size_t>(Layer2D::Count)> kLayerNames = {"World", "Ui", "Window", "Cover"};

constexpr std::array<Layer2D, static_cast<std::size_t>(Pass::Count)> kPassLayers = {
    Layer2D::Ui,     // Unnamed
    Layer2D::World,  // TextureAnimation
    Layer2D::World,  // Sky
    Layer2D::World,  // Ground
    Layer2D::World,  // Fishing
    Layer2D::World,  // Water
    Layer2D::Window, // DepthOfField
    Layer2D::World,  // Characters
    Layer2D::Window, // Shadows
    Layer2D::World,  // MovingParts
    Layer2D::World,  // Effects
    Layer2D::World,  // LensFlare
    Layer2D::Ui,     // Overlay
    Layer2D::Ui,     // Menu
    Layer2D::Window, // Fade
    Layer2D::Ui,     // Title
    Layer2D::World,  // Opening
    Layer2D::World,  // Attract
    Layer2D::Ui,     // Screen
};

Pass                         g_pass = Pass::Unnamed;
int                          g_layer = -1;
bool                         g_layer_differs = false;
std::uint64_t                g_layer_differing = 0;
std::uint64_t                g_layer_checked = 0;
std::uint64_t                g_route_checked = 0;
std::uint64_t                g_mesh_checked = 0;
std::uint64_t                g_mesh_lost = 0;
std::uint64_t                g_route_lost = 0;
std::uint64_t                g_copy_unsaid = 0;
std::uint64_t                g_layer_wrong = 0;
bool                         g_tracing = false;
std::string                  g_path;
std::vector<Span>            g_spans;
std::map<std::string, Shape> g_shapes;
std::uint64_t                g_ticks = 0;
std::uint64_t                g_counted = 0;
std::uint64_t                g_drawn = 0;
std::uint64_t                g_mismatched_ticks = 0;
std::uint64_t                g_noktus_checked = 0;   // ticks whose passes were checked against what Noktus recorded
std::uint64_t                g_noktus_differing = 0;
std::map<std::tuple<std::string, std::string, std::string>, std::uint64_t> g_classes; // pass, class, rule: Noktus's draws
std::uint64_t                g_unnamed = 0;
std::uint64_t                g_outside = 0;
std::map<std::string, std::uint64_t> g_kinds;
std::vector<Step>            g_tick_steps;             // the steps begun this tick, in order
std::uint64_t                g_steps_checked = 0;      // ticks whose steps were checked against what Noktus recorded
std::uint64_t                g_steps_differing = 0;
std::map<std::string, std::uint64_t> g_step_uses;    // a step's name and what Noktus found it reads and writes: how many times
bool                         g_step_open = false;

constexpr std::array<const char *, static_cast<std::size_t>(Step::Count)> kStepNames = {"Water", "DepthOfField", "Haze", "DepthReads"};

Span &Current() {
    if (g_spans.empty() || g_spans.back().pass != g_pass) {
        g_spans.push_back({.pass = g_pass});
    }
    return g_spans.back();
}

void WriteTrace() {
    std::ofstream out(g_path);
    out << "{\n \"ticks\": " << g_ticks << ",\n \"counted\": " << g_counted << ",\n \"drawn\": " << g_drawn
        << ",\n \"mismatched_ticks\": " << g_mismatched_ticks << ",\n \"noktus_checked\": " << g_noktus_checked << ",\n \"noktus_differing\": " << g_noktus_differing << ",\n \"unnamed\": " << g_unnamed << ",\n \"outside\": " << g_outside << ",\n \"mesh_checked\": " << g_mesh_checked << ",\n \"mesh_lost\": " << g_mesh_lost << ",\n \"route_checked\": " << g_route_checked << ",\n \"route_lost\": " << g_route_lost << ",\n \"layer_checked\": " << g_layer_checked << ",\n \"layer_wrong\": " << g_layer_wrong << ",\n \"layer_differing\": " << g_layer_differing << ",\n \"shapes\": [\n";
    bool first = true;
    for (const auto &[key, shape] : g_shapes) {
        out << (first ? "" : ",\n") << "  {\"ticks\": " << shape.ticks << ", \"passes\": [";
        first = false;
        std::size_t index = 0;
        for (std::size_t start = 0; start < key.size(); index++) {
            const std::size_t end = key.find(' ', start);
            out << (index != 0 ? ", " : "") << "{\"pass\": \"" << key.substr(start, end - start)
                << "\", \"draws\": " << shape.draws[index] / shape.ticks << ", \"copies\": " << shape.copies[index] / shape.ticks
                << "}";
            start = end + 1;
        }
        out << "]}";
    }
    out << "\n ],\n \"copy_unsaid\": " << g_copy_unsaid << ",\n \"kinds\": {";
    first = true;
    for (const auto &[key, count] : g_kinds) {
        out << (first ? "" : ", ") << "\"" << key << "\": " << count;
        first = false;
    }
    out << "},\n \"classes\": [";
    first = true;
    for (const auto &[key, draws] : g_classes) {
        out << (first ? "\n" : ",\n") << "  {\"pass\": \"" << std::get<0>(key) << "\", \"class\": \"" << std::get<1>(key) << "\", \"rule\": \"" << std::get<2>(key)
            << "\", \"draws\": " << draws << "}";
        first = false;
    }
    out << "\n ],\n \"steps_checked\": " << g_steps_checked << ",\n \"steps_differing\": " << g_steps_differing << ",\n \"steps\": [";
    first = true;
    for (const auto &[key, count] : g_step_uses) {
        const std::size_t colon = key.find(':');
        out << (first ? "\n" : ",\n") << "  {\"step\": \"" << key.substr(0, colon) << "\", \"uses\": \"" << key.substr(colon + 2) << "\", \"ticks\": " << count << "}";
        first = false;
    }
    out << "\n ]\n}\n";
}

// What Noktus found a step reads and writes, in words.
std::string StepUses(const MirrorStepReport &step) {
    const auto side = [](bool screen, bool depth, std::uint32_t textures) {
        std::string out = screen && depth ? "the screen's colour and depth" : screen ? "the screen's colour" : depth ? "the screen's depth" : "";
        if (textures != 0) {
            out += (out.empty() ? "" : ", ") + std::to_string(textures) + (textures == 1 ? " texture" : " textures");
        }
        return out.empty() ? std::string("nothing") : out;
    };
    return "reads " + side(step.reads_screen, step.reads_screen_depth, step.textures_read) + "; writes " +
           side(step.writes_screen, step.writes_screen_depth, step.textures_written);
}

} // namespace

Layer2D CurrentLayer2D() {
    return g_layer >= 0 ? static_cast<Layer2D>(g_layer) : kPassLayers[static_cast<std::size_t>(g_pass)];
}

Layer2DScope::Layer2DScope(Layer2D layer, bool differs_from_rule) : previous_(g_layer), previous_differs_(g_layer_differs) {
    g_layer = static_cast<int>(layer);
    g_layer_differs = differs_from_rule;
}

Layer2DScope::~Layer2DScope() {
    g_layer = previous_;
    g_layer_differs = previous_differs_;
}

const char *PassName(Pass pass) {
    return kNames[static_cast<std::size_t>(pass)];
}

void SetPass(Pass pass) {
    g_pass = pass;
    MirrorSetPass(pass);
}

const char *StepName(Step step) {
    return step < Step::Count ? kStepNames[static_cast<std::size_t>(step)] : "?";
}

void BeginStep(Step step) {
    if (g_tracing) {
        g_tick_steps.push_back(step);
    }
    g_step_open = true;
    MirrorBeginStep(step);
}

void EndStep() {
    if (g_step_open) {
        g_step_open = false;
        MirrorEndStep();
    }
}

Pass CurrentPass() {
    return g_pass;
}

void CountDraw() {
    if (g_tracing) {
        Current().draws++;
    }
}

void NoteCopy(gfx::Rect src_rect, gfx::Rect dst_rect) {
    if (!g_tracing) {
        return;
    }
    g_copy_unsaid += src_rect.w < 0 || src_rect.h < 0 || dst_rect.w < 0 || dst_rect.h < 0 || src_rect.x < 0 || src_rect.y < 0 || dst_rect.x < 0 || dst_rect.y < 0;
}

void CountCopy() {
    if (g_tracing) {
        Current().copies++;
    }
}

namespace {

struct Shape2D {
    Layer2D rule = Layer2D::Ui;
    bool    covers = false;
    bool    on_main = false;
    bool    has_edges = false; // false: a surface with nothing past its frame, where every layer means the same
    bool    one_quad = false;  // Noktus takes a layer for a whole draw, and a cover stretches every rectangle in it
    bool    tests_depth = false;
};

// The renderer's rules for a 2D draw (Kazarov's gfx/draw.cpp, Draw2DSided), as it decides them from shape and state.
Shape2D ReadShape(gfx::Primitive primitive, std::span<const gfx::Vertex2D> vertices, const gfx::TextureBinding &texture, const gfx::DrawState &state) {
    const gfx::TextureHandle target = gfx::CurrentRenderTarget();
    const auto               info = gfx::GetTextureInfo(texture.texture);
    const bool               samples_frame = texture.texture == gfx::kPreviousFrame ||
                               (info && (info->shares_main_depth || info->frame_target));
    const bool ui = state.depth_test == gfx::DepthTest::Always && !state.depth_write && !samples_frame;
    const bool untextured = texture.texture == gfx::kNullTexture;
    const bool quads = primitive == gfx::Primitive::Quads || (primitive == gfx::Primitive::TriangleStrip && vertices.size() == 4);
    const auto             drawn_on = gfx::GetTextureInfo(target);
    const bool             on_main = target == gfx::kMainTarget;
    const gfx::LogicalRect frame = on_main || !drawn_on ? gfx::LogicalFrame()
                                                        : gfx::LogicalRect{0.0f, 0.0f, static_cast<float>(drawn_on->width), static_cast<float>(drawn_on->height)};
    Shape2D shape;
    shape.on_main = on_main;
    shape.has_edges = on_main || (drawn_on && (drawn_on->shares_main_depth || drawn_on->frame_target));
    shape.one_quad = vertices.size() == 4;
    shape.tests_depth = state.depth_test != gfx::DepthTest::Always;
    bool reaches = false;
    if (quads) {
        for (std::size_t first = 0; first + 4 <= vertices.size(); first += 4) {
            float low_x = INFINITY, low_y = INFINITY, high_x = -INFINITY, high_y = -INFINITY;
            for (std::size_t i = first; i < first + 4; i++) {
                low_x = std::min(low_x, vertices[i].x);
                high_x = std::max(high_x, vertices[i].x);
                low_y = std::min(low_y, vertices[i].y);
                high_y = std::max(high_y, vertices[i].y);
            }
            shape.covers |= low_x <= frame.x && high_x >= frame.x + frame.w && low_y <= frame.y && high_y >= frame.y + frame.h;
            reaches |= (low_x <= frame.x && high_x > frame.x) || (high_x >= frame.x + frame.w && low_x < frame.x + frame.w) ||
                       (low_y <= frame.y && high_y > frame.y) || (high_y >= frame.y + frame.h && low_y < frame.y + frame.h);
        }
    }
    shape.rule = ui ? Layer2D::Ui : Layer2D::World;
    if (shape.covers && !untextured && !samples_frame) {
        shape.rule = Layer2D::Cover;
    } else if (reaches && (untextured || samples_frame)) {
        shape.rule = Layer2D::Window;
    }
    return shape;
}

// The interface takes Kazarov's answer wherever it reaches past the frame. The scene takes it only for a fill or a
// picture of the frame laid over everything without depth (the opening's Dark Genie blurs the frame before over the
// frame), which would otherwise stop at the 4:3 frame: a scene's full-frame picture isn't stretched, since what the
// scene lays over it stays in the frame, and nothing tested against depth leaves the scene's class.
Layer2D Settled(Layer2D named, const Shape2D &shape) {
    if (g_layer_differs || !shape.has_edges) {
        return named;
    }
    if (named == Layer2D::Ui && (shape.rule == Layer2D::Window || (shape.rule == Layer2D::Cover && shape.one_quad))) {
        return shape.rule;
    }
    if (named == Layer2D::World && shape.rule == Layer2D::Window && !shape.tests_depth) {
        return Layer2D::Window;
    }
    return named;
}

} // namespace

Layer2D DrawLayer2D(gfx::Primitive primitive, std::span<const gfx::Vertex2D> vertices, const gfx::TextureBinding &texture, const gfx::DrawState &state) {
    const Layer2D named = CurrentLayer2D();
    const bool    settles = named == Layer2D::Ui || named == Layer2D::World;
    return settles && !vertices.empty() ? Settled(named, ReadShape(primitive, vertices, texture, state)) : named;
}

void Note2D(gfx::Primitive primitive, std::span<const gfx::Vertex2D> vertices, const gfx::TextureBinding &texture,
            const gfx::DrawState &state) {
    if (!g_tracing || vertices.empty()) {
        return;
    }
    const Shape2D shape = ReadShape(primitive, vertices, texture, state);
    if (!shape.has_edges) {
        return;
    }
    const Layer2D declared = Settled(CurrentLayer2D(), shape);
    const Layer2D rule = shape.rule;
    const bool    covers = shape.covers;
    const bool    on_main = shape.on_main;
    // A window-wide draw tiled like a fade has tiles that reach no edge: only the rule's layers that can't be a tile of it disagree.
    // Off the screen nothing follows the interface size, so Ui and World are one.
    const auto stays = [](Layer2D layer) { return layer == Layer2D::Ui || layer == Layer2D::World; };
    const bool agrees = declared == rule || (declared == Layer2D::Window && !covers) || (!on_main && stays(declared) && stays(rule));
    // The scene keeps its full-frame pictures, and anything tested against depth, in the frame on purpose (Settled).
    const bool scene_keeps = declared == Layer2D::World && (rule == Layer2D::Cover || (rule == Layer2D::Window && shape.tests_depth));
    g_layer_checked++;
    if (!agrees && (g_layer_differs || scene_keeps)) {
        g_layer_differing++;
    } else if (!agrees) {
        g_layer_wrong++;
        std::string key = std::string(PassName(g_pass)) + " " + kLayerNames[static_cast<std::size_t>(declared)] + " is " +
                          kLayerNames[static_cast<std::size_t>(rule)];
#if defined(DC_PASS_STACKS)
        void *stack[12];
        const int depth = backtrace(stack, 12);
        Dl_info   info{};
        for (int i = 3; i < depth; i++) {
            if (dladdr(stack[i], &info) != 0) {
                key += std::format(" {:x}", reinterpret_cast<std::uintptr_t>(stack[i]) - reinterpret_cast<std::uintptr_t>(info.dli_fbase));
            }
        }
#endif
        g_kinds[key]++;
    }
}

Routed Route(const gfx::TextureBinding &texture, const gfx::DrawState &state) {
    Routed routed;
    FromMaterial(ToMaterial(state, texture), routed.state, routed.texture);
    g_route_checked++;
    if (!DrawsTheSame(state, texture, routed.state, routed.texture)) {
        // A draw a material can't say yet stays as it was, on the screens the scene set doesn't reach. A trace counts it.
        g_route_lost++;
        return {state, texture};
    }
    return routed;
}

void NoteMesh(const gfx::MeshConstants &constants, const gfx::MeshTransform *transform, const gfx::TextureBinding &texture,
              const gfx::DrawState &state) {
    if (!g_tracing) {
        return;
    }
    g_mesh_checked++;
    const char *difference = transform == nullptr ? "no transform" : MeshSceneDifference(ToMeshScene(constants, *transform, texture, state), constants);
    if (difference != nullptr) {
        g_mesh_lost++;
        g_kinds[std::string("mesh ") + difference + " flags " + std::to_string(constants.flags) + " " + PassName(g_pass)]++;
    }
}

void PassTraceStart() {
    const char *path = std::getenv("DC_PASS_TRACE");
    if (path == nullptr || *path == '\0') {
        return;
    }
    g_path = path;
    g_tracing = true;
    std::atexit(WriteTrace);
}

// Whatever was counted before a tick starts went straight to the screen, as the loading screen does, in no tick's list.
void PassTraceBeginTick() {
    for (const Span &span : g_spans) {
        g_outside += span.draws;
    }
    g_spans.clear();
    g_tick_steps.clear(); // a step begun outside a tick never reaches Noktus
}

void PassTraceTick(std::uint32_t drawn, bool list_has_draws) {
    if (!g_tracing) {
        return;
    }
    std::string   key;
    std::uint32_t counted = 0;
    for (const Span &span : g_spans) {
        key += PassName(span.pass);
        key += ' ';
        counted += span.draws;
        if (span.pass == Pass::Unnamed) {
            g_unnamed += span.draws;
        }
    }
    Shape &shape = g_shapes[key];
    shape.draws.resize(g_spans.size());
    shape.copies.resize(g_spans.size());
    shape.ticks++;
    for (std::size_t i = 0; i < g_spans.size(); i++) {
        shape.draws[i] += g_spans[i].draws;
        shape.copies[i] += g_spans[i].copies;
    }
    g_ticks++;
    g_counted += counted;
    g_drawn += drawn;
    g_mismatched_ticks += list_has_draws && counted != drawn ? 1 : 0;
    // Noktus is told each pass as it's named, and has to have recorded the same runs of draws and copies.
    if (MirrorOn()) {
        const std::vector<MirrorPassStretch> recorded = MirrorTickPasses();
        bool                                 same = recorded.size() == g_spans.size();
        std::string                          theirs;
        for (std::size_t i = 0; i < recorded.size(); i++) {
            theirs += std::string(PassName(recorded[i].pass)) + ":" + std::to_string(recorded[i].draws) + "+" + std::to_string(recorded[i].copies) + " ";
            same = same && recorded[i].pass == g_spans[i].pass && recorded[i].draws == g_spans[i].draws && recorded[i].copies == g_spans[i].copies;
        }
        g_noktus_checked++;
        for (const MirrorClassTally &counted : MirrorTakeClassTally()) {
            g_classes[{PassName(counted.pass), counted.draw_class, counted.rule}] += counted.draws;
        }
        if (!same) {
            std::string ours;
            for (const Span &span : g_spans) {
                ours += std::string(PassName(span.pass)) + ":" + std::to_string(span.draws) + "+" + std::to_string(span.copies) + " ";
            }
            g_noktus_differing++;
            g_kinds["noktus passes " + ours + "| " + theirs]++;
        }
        // And the same steps, in order, each of which it says what it reads and writes.
        const std::vector<MirrorStepReport> steps = MirrorTickSteps();
        bool                                steps_same = steps.size() == g_tick_steps.size();
        for (std::size_t i = 0; i < steps.size(); i++) {
            steps_same = steps_same && steps[i].step == g_tick_steps[i];
            g_step_uses[std::string(StepName(steps[i].step)) + ": " + StepUses(steps[i])]++;
        }
        g_steps_checked++;
        g_steps_differing += steps_same ? 0 : 1;
    }
    g_tick_steps.clear();
    g_spans.clear();
}

} // namespace dc::render
