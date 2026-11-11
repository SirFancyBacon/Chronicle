#pragma once

#include <cstdint>

namespace dc::render {

/** What a GPU has room for: its own memory in all, and what the system lets the game have of it now (0 if the driver can't say). */
struct GpuRoom {
    std::uint64_t heap = 0;
    std::uint64_t budget = 0;
};

/**
 * The most samples a pixel, of 1, 2 and 4, whose pictures fit in the GPU's memory at this window size: the game's effects keep pictures as large as
 * the picture they're drawn into, so their memory grows with the samples. temporal is for anti-aliasing that keeps frames, which keeps more pictures.
 * 4 if nothing's known of the GPU.
 */
int MostSupersamplingFor(const GpuRoom &room, std::uint32_t window_width, std::uint32_t window_height, bool temporal);

} // namespace dc::render
