#include "sound/places.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace dc::sound {

namespace {

constexpr std::size_t kMostPlaces = 16;

float DistanceSquared(const std::array<float, 3> &a, const std::array<float, 3> &b) {
    const float x = a[0] - b[0];
    const float y = a[1] - b[1];
    const float z = a[2] - b[2];
    return (x * x) + (y * y) + (z * z);
}

} // namespace

int SplitAmong(std::span<const float> volumes, std::span<const std::array<float, 3>> positions, std::span<const std::array<float, 3>> was,
               std::span<SplitVoice> out) {
    const std::size_t places = std::min({volumes.size(), positions.size(), kMostPlaces});
    const std::size_t count = std::min(places, out.size());
    if (count == 0) {
        return 0;
    }

    std::array<int, kMostPlaces> order{};
    for (std::size_t j = 0; j < places; j++) {
        order[j] = static_cast<int>(j);
    }
    // Ties to the place given first, so the choice doesn't wander between equals.
    std::partial_sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(count), order.begin() + static_cast<std::ptrdiff_t>(places),
                      [&](int a, int b) { return volumes[a] > volumes[b] || (volumes[a] == volumes[b] && a < b); });

    float total = 0.0f;
    for (std::size_t j = 0; j < places; j++) {
        total += volumes[j];
    }
    total = std::min(total, 1.0f);
    float power = 0.0f;
    for (std::size_t k = 0; k < count; k++) {
        power += volumes[order[k]] * volumes[order[k]];
    }
    const float gain = power > 0.0f ? total / std::sqrt(power) : 0.0f;

    std::array<bool, kMostPlaces> taken{};
    for (std::size_t voice = 0; voice < count; voice++) {
        std::size_t best = count;
        float       best_distance = 0.0f;
        for (std::size_t k = 0; k < count; k++) {
            if (taken[k]) {
                continue;
            }
            if (voice >= was.size()) {
                best = k; // a new voice: the loudest left
                break;
            }
            const float distance = DistanceSquared(positions[order[k]], was[voice]);
            if (best == count || distance < best_distance) {
                best = k;
                best_distance = distance;
            }
        }
        taken[best] = true;
        out[voice] = {.place = order[best], .volume = volumes[order[best]] * gain};
    }
    return static_cast<int>(count);
}

} // namespace dc::sound
