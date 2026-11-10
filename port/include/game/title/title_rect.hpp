#pragma once

namespace dc::game {

/**
 * The title overlay's rectangle: x, y, width and height, built from them in that order (rect.hpp's CRect_i_ has the
 * same layout, but its constructor takes them in another). Retail's title units each declared it, some 16-byte aligned
 * and some not, which MWCC didn't mind. Here every unit's CRect<int> is one class, so it's declared once: with two
 * alignments, code built for the aligned one (op_a.cpp's MoveImageTest, copying it with an aligned SSE load) faulted on
 * a rectangle the other built where nothing promised 16 bytes, as rushmovi.cpp's face frames on Windows did.
 */
template <class T>
class CRect {
public:
    T x; /**< Left edge in pixels. */
    T y; /**< Top edge in pixels. */
    T w; /**< Width in pixels. */
    T h; /**< Height in pixels. */

    CRect() : x(0), y(0), w(0), h(0) {}

    CRect(T new_x, T new_y, T new_width, T new_height) : x(new_x), y(new_y), w(new_width), h(new_height) {}
} __attribute__((aligned(16)));

} // namespace dc::game
