#include "render/passes.hpp"

#include <array>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

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

Pass                         g_pass = Pass::Unnamed;
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

Span &Current() {
    if (g_spans.empty() || g_spans.back().pass != g_pass) {
        g_spans.push_back({.pass = g_pass});
    }
    return g_spans.back();
}

void WriteTrace() {
    std::ofstream out(g_path);
    out << "{\n \"ticks\": " << g_ticks << ",\n \"counted\": " << g_counted << ",\n \"drawn\": " << g_drawn
        << ",\n \"mismatched_ticks\": " << g_mismatched_ticks << ",\n \"unnamed\": " << g_unnamed << ",\n \"outside\": " << g_outside << ",\n \"shapes\": [\n";
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
    out << "\n ]\n}\n";
}

} // namespace

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
