#include "render/supersampling.hpp"

#include <array>

namespace dc::render {

namespace {

// GPU memory a pixel of the picture costs once the game has shown its effects, measured at its most from the title into Norune at 3840x2400 (most of it
// the effects' own pictures): the rise from no supersampling (1515 MB) to 2x (2993 MB), over the pixels that added, with surfaces' depth made only
// when needed.
constexpr double kBytesPerPixel = 170.0;
// TAA and SMAA T2x keep frames as well: SMAA T2x, the more, added 36 bytes a pixel.
constexpr double kTemporalBytesPerPixel = 206.0;
// Headroom for what doesn't grow with the picture: textures, meshes, what the driver keeps and the room in VMA's blocks, measured at a few tens of MB,
// and a town's or a dungeon's more.
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
