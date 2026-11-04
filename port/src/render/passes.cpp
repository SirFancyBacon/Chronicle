#include "render/passes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

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
std::uint64_t                g_layer_wrong = 0;
bool                         g_tracing = false;
std::string                  g_path;
std::vector<Span>            g_spans;
std::map<std::string, Shape> g_shapes;
std::uint64_t                g_ticks = 0;
std::uint64_t                g_counted = 0;
std::uint64_t                g_drawn = 0;
std::uint64_t                g_mismatched_ticks = 0;
std::uint64_t                g_unnamed = 0;
std::uint64_t                g_outside = 0;
std::map<std::string, std::uint64_t> g_kinds;

Span &Current() {
    if (g_spans.empty() || g_spans.back().pass != g_pass) {
        g_spans.push_back({.pass = g_pass});
    }
    return g_spans.back();
}

void WriteTrace() {
    std::ofstream out(g_path);
    out << "{\n \"ticks\": " << g_ticks << ",\n \"counted\": " << g_counted << ",\n \"drawn\": " << g_drawn
        << ",\n \"mismatched_ticks\": " << g_mismatched_ticks << ",\n \"unnamed\": " << g_unnamed << ",\n \"outside\": " << g_outside << ",\n \"layer_checked\": " << g_layer_checked << ",\n \"layer_wrong\": " << g_layer_wrong << ",\n \"layer_differing\": " << g_layer_differing << ",\n \"shapes\": [\n";
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
    out << "\n ],\n \"kinds\": {";
    first = true;
    for (const auto &[key, count] : g_kinds) {
        out << (first ? "" : ", ") << "\"" << key << "\": " << count;
        first = false;
    }
    out << "}\n}\n";
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
}

Pass CurrentPass() {
    return g_pass;
}

void CountDraw() {
    if (g_tracing) {
        Current().draws++;
    }
}

void CountCopy() {
    if (g_tracing) {
        Current().copies++;
    }
}

// The renderer's rules for a 2D draw (Kazarov's gfx/draw.cpp, Draw2DSided), as it decides them from shape and state.
void Note2D(gfx::Primitive primitive, std::span<const gfx::Vertex2D> vertices, const gfx::TextureBinding &texture,
            const gfx::DrawState &state) {
    if (!g_tracing || vertices.empty()) {
        return;
    }
    const gfx::TextureHandle target = gfx::CurrentRenderTarget();
    const auto               info = gfx::GetTextureInfo(texture.texture);
    const bool               samples_frame = texture.texture == gfx::kPreviousFrame ||
                               (info && (info->shares_main_depth || info->frame_target));
    const bool ui = state.depth_test == gfx::DepthTest::Always && !state.depth_write && !samples_frame;
    const bool untextured = texture.texture == gfx::kNullTexture;
    const bool quads = primitive == gfx::Primitive::Quads || (primitive == gfx::Primitive::TriangleStrip && vertices.size() == 4);
    const gfx::LogicalRect frame = gfx::LogicalFrame();
    bool covers = false;
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
            covers |= low_x <= frame.x && high_x >= frame.x + frame.w && low_y <= frame.y && high_y >= frame.y + frame.h;
            reaches |= (low_x <= frame.x && high_x > frame.x) || (high_x >= frame.x + frame.w && low_x < frame.x + frame.w) ||
                       (low_y <= frame.y && high_y > frame.y) || (high_y >= frame.y + frame.h && low_y < frame.y + frame.h);
        }
    }
    if (target != gfx::kMainTarget) {
        return;
    }
    const Layer2D declared = CurrentLayer2D();
    Layer2D       rule = ui ? Layer2D::Ui : Layer2D::World;
    if (covers && !untextured && !samples_frame) {
        rule = Layer2D::Cover;
    } else if (reaches && (untextured || samples_frame)) {
        rule = Layer2D::Window;
    }
    // A window-wide draw tiled like a fade has tiles that reach no edge: only the rule's layers that can't be a tile of it disagree.
    const bool agrees = declared == rule || (declared == Layer2D::Window && !covers);
    g_layer_checked++;
    if (!agrees && g_layer_differs) {
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
}

void PassTraceTick(std::uint32_t drawn) {
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
    g_mismatched_ticks += counted != drawn ? 1 : 0;
    g_spans.clear();
}

} // namespace dc::render
