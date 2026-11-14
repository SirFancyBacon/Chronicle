#include "firelight.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>
#include <unordered_map>

#include "render/renderer_calls.hpp"

namespace dc::game {

namespace {

// The game gave a character a town fire's light in full within 5 units, easing to none at 40, at a brightness drawn from 80 to 127 every tick (128 the
// art's own), 104 on average. A town's torches burn 35 to 55 units above the ground, so a character under one was barely lit, and real falloff matched
// to that left Toan unlit beneath them (the owner's playtest, 2026-11-14): a fire's light is its average brightness at 15 units, reaching to 80, which
// lights a character under a torch and the wall beside it as a flame would. DC_NOKTUS_FIRE scales it, for tuning by eye, 0 for none.
constexpr float kBrightness = 104.0f / 128.0f;
constexpr float kMatchDistance = 15.0f;
constexpr float kRange = 80.0f;
constexpr float kSize = 3.0f; // the flame's own size: nearer, no brighter

float FireScale() {
    static const float scale = [] {
        const char *asked = std::getenv("DC_NOKTUS_FIRE");
        if (asked == nullptr) {
            return 1.0f;
        }
        char       *end = nullptr;
        const float wanted = std::strtof(asked, &end);
        return end != asked && *end == '\0' && std::isfinite(wanted) ? std::max(wanted, 0.0f) : 1.0f;
    }();
    return scale;
}

// Flickering as a fire does, gently: the light a share brighter or dimmer, a new share every few ticks, eased towards it each tick. The game drew a new
// brightness every tick, which with light added as light reads as a strobe (the owner's report, 2026-11-13), and on the walls a fire lights, as too
// quick even eased (2026-11-14). From numbers of its own, so the game's random numbers stay in step.
constexpr float kFlickerShare = 0.06f;
constexpr int   kFlickerTicks = 8;
constexpr float kFlickerEase = 0.1f;

struct Flicker {
    float share = 1.0f;
    float towards = 1.0f;
    int   ticks = 0;
};

float Flickering(std::uint64_t key) {
    static std::minstd_rand                            numbers;
    static std::uniform_real_distribution<float>       drawn(1.0f - kFlickerShare, 1.0f + kFlickerShare);
    static std::unordered_map<std::uint64_t, Flicker> fires;
    Flicker &fire = fires[key];
    if (fire.ticks == 0) {
        fire.towards = drawn(numbers);
    }
    fire.ticks = (fire.ticks + 1) % kFlickerTicks;
    fire.share += (fire.towards - fire.share) * kFlickerEase;
    return fire.share;
}

float LinearOf(float encoded) {
    const float c = std::max(encoded, 0.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

} // namespace

void GiveFireLight(std::uint64_t key, const float *flame) {
    if (!render::ModernLight() || FireScale() == 0.0f) {
        return;
    }
    const float light = LinearOf(kBrightness) * kMatchDistance * kMatchDistance * FireScale() * Flickering(key);
    render::AddPointLight({.key = key, .position = {flame[0], flame[1], flame[2]}, .color = {light, light, light}, .range = kRange, .size = kSize});
}

} // namespace dc::game
