#include "firelight.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_map>

#include "render/renderer_calls.hpp"

namespace dc::game {

namespace {

// The game gave a character a town fire's light in full within 5 units, easing to none at 40, at a brightness drawn from 80 to 127 every tick (128 the
// art's own). Matched here at ten units, where a character walking by gets most of it.
constexpr float kMatchDistance = 10.0f;
constexpr float kMatchShare = 1.0f - (kMatchDistance - 5.0f) / (40.0f - 5.0f);
constexpr float kRange = 40.0f;
constexpr float kSize = 2.0f; // the flame's own size: nearer, no brighter

// Flickering as a fire does: a new brightness every few ticks, eased towards each tick. The game drew a new one every tick, which with light added as
// light reads as a strobe (the owner's report, 2026-11-13). From numbers of its own, so the game's random numbers stay in step.
constexpr int   kFlickerTicks = 6;
constexpr float kFlickerEase = 0.25f;

struct Flicker {
    float brightness = 0.0f;
    float towards = 0.0f;
    int   ticks = 0;
};

float Flickering(std::uint64_t key) {
    static std::minstd_rand                       numbers;
    static std::unordered_map<std::uint64_t, Flicker> fires;
    Flicker    &fire = fires[key];
    const float drawn = 80.0f + static_cast<float>(numbers() % 48);
    if (fire.ticks == 0) {
        fire.brightness = fire.towards = drawn;
    }
    if (fire.ticks++ % kFlickerTicks == 0) {
        fire.towards = drawn;
    }
    fire.brightness += (fire.towards - fire.brightness) * kFlickerEase;
    return fire.brightness;
}

float LinearOf(float encoded) {
    const float c = std::max(encoded, 0.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

} // namespace

void GiveFireLight(std::uint64_t key, const float *flame) {
    if (!render::ModernLight()) {
        return;
    }
    const float light = LinearOf(Flickering(key) / 128.0f * kMatchShare) * kMatchDistance * kMatchDistance;
    render::AddPointLight({.key = key, .position = {flame[0], flame[1], flame[2]}, .color = {light, light, light}, .range = kRange, .size = kSize});
}

} // namespace dc::game
