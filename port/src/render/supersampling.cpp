#include "render/supersampling.hpp"

#include <array>

namespace dc::render {

namespace {

// GPU memory a pixel of the picture costs once the game has shown its effects, measured at its most over the whole opening into Norune at 3840x2400
// (most of it the effects' own pictures, made once and kept): the rise from no supersampling (1946 MB) to 2x (3858 MB), over the pixels that added.
constexpr double kBytesPerPixel = 218.0;
// TAA and SMAA T2x keep frames as well: SMAA T2x, the more, rose to 2258 MB.
constexpr double kTemporalBytesPerPixel = 254.0;
// What doesn't grow with the picture: textures, meshes and what the driver keeps, with room for a town's or a dungeon's more.
constexpr double kFixedBytes = 512.0 * 1024.0 * 1024.0;
// Of what the system allows: some it lends to others as it goes.
constexpr double kShareOfBudget = 0.9;
// Without a budget the desktop's share is unknown, so less of the memory is counted on.
constexpr double kShareOfHeap = 0.7;

} // namespace

int MostSupersamplingFor(const GpuRoom &room, std::uint32_t window_width, std::uint32_t window_height, bool temporal) {
    const double allowed = room.budget != 0 ? kShareOfBudget * static_cast<double>(room.budget) : kShareOfHeap * static_cast<double>(room.heap);
    if (allowed <= 0.0 || window_width == 0 || window_height == 0) {
        return 4;
    }
    const double pixels = static_cast<double>(window_width) * static_cast<double>(window_height);
    const double per_pixel = temporal ? kTemporalBytesPerPixel : kBytesPerPixel;
    constexpr std::array kSamples = {4, 2};
    for (const int samples : kSamples) {
        if (kFixedBytes + per_pixel * pixels * samples <= allowed) {
            return samples;
        }
    }
    // Off is always offered: a window too large even for that is the system's to refuse.
    return 1;
}

} // namespace dc::render
