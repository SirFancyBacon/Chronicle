#pragma once

// Derived from Kazarov's platform/ttffont.hpp, by TheMoonPeople, under the MIT licence: see LICENSE.kazarov beside this file. It's here so that its textures
// go through the renderer's calls, whichever renderer draws.

#include <filesystem>
#include <span>
#include <vector>

#include "render/types.hpp"

// A TrueType font for the game's message text, drawn at the screen's own resolution instead of
// from the 14x20 bitmaps of gaiji.img. Host side: plain types only.
namespace dc::ttffont {

// Reads the font file; false (with a message on stderr) for one that is missing or not TrueType.
// Loading again replaces the font.
bool Load(const std::filesystem::path &file);

bool Loaded();

// One glyph in the atlas. u and v are texel coordinates of the bitmap in the atlas; the bitmap is
// width x height pixels, its top-left x_offset pixels right of the pen and y_offset pixels below
// the baseline (negative above it).
struct Glyph {
    float u0, v0, u1, v1;
    float x_offset, y_offset;
    float width, height;
};

// The glyph of ch at an em of em_px pixels, rasterized into the atlas when first asked for. False
// where the font has no glyph for ch, so the caller draws what it drew before, or the atlas is full.
bool GetGlyph(char32_t ch, int em_px, Glyph &out);

// The glyph of ch at an em of em_px pixels as plain coverage (alpha, w x h, 0 to 255) with its top-left
// corner x0, y0 pixels from the pen on the baseline, y down; what GetGlyph puts in the atlas, and what
// the tests look at. False where GetGlyph would say so.
bool RenderGlyph(char32_t ch, int em_px, std::vector<unsigned char> &alpha, int &w, int &h, int &x0, int &y0);

// One tap of a drop shadow: the glyph moved dx, dy of its own pixels (y down) at an opacity of alpha, 0 to 1.
struct ShadowTap {
    float dx, dy, alpha;
};

// The taps laid one over another, as quads of the glyph drawn in order would lay them, as one picture: what a shadow of
// many taps costs in one quad. As near as the filter lets it be: the taps are filtered at the screen's pixels and then
// laid, the picture laid at the glyph's and then filtered, so where the glyph is squeezed or off the pixel grid they
// differ a little, smoothly. Its coverage (w x h) is the glyph's, as RenderGlyph gives it; the picture is width x height
// with its top-left left, top pixels from the glyph's.
std::vector<unsigned char> ShadowCoverage(const std::vector<unsigned char> &alpha, int w, int h, std::span<const ShadowTap> taps, int &left,
                                          int &top, int &width, int &height);

// The shadow of ch at an em of em_px pixels painted into the atlas, white with ShadowCoverage's as alpha, so the draw's
// vertex colour is its colour; x_offset and y_offset are of its top-left from the glyph's, in the glyph's pixels. Each
// tap's opacity is at most 1, as drawn. The taps are rounded to a quarter of a pixel. Painted when first asked for, which
// may start the atlas again (see Generation). False where GetGlyph would be.
bool GetShadow(char32_t ch, int em_px, std::span<const ShadowTap> taps, Glyph &out);

// Counts the atlas's restarts, when it fills: a glyph or shadow got before one has coordinates that show something else now.
unsigned Generation();

// The atlas, to draw GetGlyph's coordinates from.
render::TextureBinding Binding();

// The font's own proportions, as fractions of an em.
struct Metrics {
    float ascent;
    float descent; // positive, below the baseline
    float advance; // of the digit 0
};

Metrics GetMetrics();

} // namespace dc::ttffont
