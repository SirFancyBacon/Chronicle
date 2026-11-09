// Derived from Kazarov's tests/ttffont_test.cpp, by TheMoonPeople, under the MIT licence: see port/src/text/LICENSE.kazarov.
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "platform/settings.hpp"
#include "text/ttffont.hpp"

// The tests run on the TrueType font named by <prefix>_TEST_FONT and skip without it.
// <prefix>_TEST_FONT_DUMP=1 also prints each glyph as text.

namespace {

struct Rendered {
    std::vector<unsigned char> alpha;
    int                        w = 0;
    int                        h = 0;
    int                        x0 = 0;
    int                        y0 = 0;
};

bool LoadTestFont() {
    const char *path = SettingsEnv("TEST_FONT");
    return path != nullptr && *path != '\0' && dc::ttffont::Load(path);
}

Rendered Render(char32_t ch, int em_px = 40) {
    Rendered out;
    EXPECT_TRUE(dc::ttffont::RenderGlyph(ch, em_px, out.alpha, out.w, out.h, out.x0, out.y0)) << static_cast<unsigned>(ch);
    if (SettingsEnv("TEST_FONT_DUMP") != nullptr) {
        std::printf("U+%04X  %dx%d at (%d,%d)\n", static_cast<unsigned>(ch), out.w, out.h, out.x0, out.y0);
        for (int row = 0; row < out.h; row++) {
            for (int col = 0; col < out.w; col++) {
                const unsigned char a = out.alpha[static_cast<size_t>(row) * out.w + col];
                std::putchar(a > 160 ? '#' : a > 64 ? '+'
                                                    : ' ');
            }
            std::putchar('\n');
        }
    }
    return out;
}

} // namespace

// An accented letter is its base with a mark above it, in the same place across the letters.
TEST(TextTtfFont, AccentedLettersAreBuiltFromTheFontsOwnGlyphs) {
    if (!LoadTestFont()) {
        GTEST_SKIP() << "TEST_FONT is not set";
    }
    const Rendered e = Render(U'e');
    for (char32_t accented : {U'é', U'è', U'ê', U'ë'}) {
        const Rendered r = Render(accented);
        EXPECT_GT(r.w, 0);
        EXPECT_LT(r.y0, e.y0) << "the mark stands above the letter";
        EXPECT_EQ(r.y0 + r.h, e.y0 + e.h) << "the letter keeps its bottom";
        EXPECT_NEAR(r.x0 + r.w / 2.0, e.x0 + e.w / 2.0, 2.0) << "the mark is centred on the letter";
    }
    const Rendered c = Render(U'c');
    const Rendered cedilla = Render(U'ç');
    EXPECT_GT(cedilla.y0 + cedilla.h, c.y0 + c.h) << "the cedilla hangs below the baseline";
    EXPECT_EQ(cedilla.y0, c.y0);

    const Rendered upper = Render(U'É');
    const Rendered plain_upper = Render(U'E');
    EXPECT_LT(upper.y0, plain_upper.y0);
}

TEST(TextTtfFont, InvertedMarksAndLigaturesAreBuilt) {
    if (!LoadTestFont()) {
        GTEST_SKIP() << "TEST_FONT is not set";
    }
    const Rendered bang = Render(U'!');
    const Rendered inverted = Render(U'¡');
    EXPECT_EQ(inverted.w, bang.w);
    EXPECT_EQ(inverted.h, bang.h);
    // Turned over: what was at the top is now at the bottom.
    EXPECT_EQ(inverted.alpha.front(), bang.alpha[bang.alpha.size() - static_cast<size_t>(bang.w)]);

    const Rendered oe = Render(U'œ');
    const Rendered o = Render(U'o');
    EXPECT_GT(oe.w, o.w);
    EXPECT_LT(oe.w, 2 * o.w);
    Render(U'Œ');
    Render(U'Ñ');
    Render(U'ñ');
    Render(U'ä');
}

TEST(TextTtfFont, WhatItCannotBuildIsLeftToTheBitmap) {
    if (!LoadTestFont()) {
        GTEST_SKIP() << "TEST_FONT is not set";
    }
    Rendered out;
    EXPECT_FALSE(dc::ttffont::RenderGlyph(U'ß', 40, out.alpha, out.w, out.h, out.x0, out.y0));
    EXPECT_FALSE(dc::ttffont::RenderGlyph(U'~' + 0x1000, 40, out.alpha, out.w, out.h, out.x0, out.y0));
}

// A shadow of taps painted as one is what the taps' quads laid in order would leave: a tap on whole pixels is the glyph
// moved, one half a pixel off is the filter's average of two, and taps over one another let through what each doesn't cover.
TEST(TtfFontShadow, TapsLaidAsOneAreTheTapsInOrder) {
    // A 2x1 glyph: one solid pixel, one half covered.
    const std::vector<unsigned char> glyph = {255, 128};
    int                              left = 0;
    int                              top = 0;
    int                              width = 0;
    int                              height = 0;
    const auto                       at = [&](const std::vector<unsigned char> &shadow, int x, int y) {
        return static_cast<int>(shadow[static_cast<size_t>(y - top) * width + (x - left)]);
    };

    const std::vector<dc::ttffont::ShadowTap> moved = {{.dx = 2.0f, .dy = 1.0f, .alpha = 1.0f}};
    const std::vector<unsigned char>          one = dc::ttffont::ShadowCoverage(glyph, 2, 1, moved, left, top, width, height);
    ASSERT_EQ(one.size(), static_cast<size_t>(width) * height);
    EXPECT_LE(left, 0);
    EXPECT_LE(top, 0);
    EXPECT_GE(left + width, 4);
    EXPECT_GE(top + height, 2);
    EXPECT_EQ(at(one, 2, 1), 255);
    EXPECT_EQ(at(one, 3, 1), 128);
    EXPECT_EQ(at(one, 0, 0), 0);
    EXPECT_EQ(at(one, 1, 1), 0);

    const std::vector<dc::ttffont::ShadowTap> half = {{.dx = 0.5f, .dy = 0.0f, .alpha = 1.0f}};
    const std::vector<unsigned char>          averaged = dc::ttffont::ShadowCoverage(glyph, 2, 1, half, left, top, width, height);
    EXPECT_EQ(at(averaged, 0, 0), 128);       // half the solid pixel
    EXPECT_NEAR(at(averaged, 1, 0), 191, 1);  // between the two
    EXPECT_NEAR(at(averaged, 2, 0), 64, 1);   // half the half-covered one

    // Two taps of a quarter each over the solid pixel: 1 - (3/4)^2 of it is shadow.
    const std::vector<dc::ttffont::ShadowTap> stacked = {{.dx = 0.0f, .dy = 0.0f, .alpha = 0.25f}, {.dx = 0.0f, .dy = 0.0f, .alpha = 0.25f}};
    const std::vector<unsigned char>          two = dc::ttffont::ShadowCoverage(glyph, 2, 1, stacked, left, top, width, height);
    EXPECT_NEAR(at(two, 0, 0), 112, 1);

    EXPECT_TRUE(dc::ttffont::ShadowCoverage(glyph, 2, 1, {}, left, top, width, height).empty());
}
