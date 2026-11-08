#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <thread>
#include <utility>
#include <vector>

#include "gfx_fixture.hpp"
#include "platform/load_work_profile.hpp"
#include "text/captionfont.hpp"

namespace dc::test {

using namespace dc::text;

namespace {

std::filesystem::path FontFile() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "tools" / "font" /
           "ChronicleSharp-Bold.ttf";
}

capfont::Paint Look() {
    return capfont::Paint{
        .depth_x = 0.5f,
        .depth_y = 0.75f,
        .stroke = 0.2f,
        .strokes = 3,
        .outline = 1.0f,
        .fill = {232, 230, 216},
        .body = {52,  56,  192},
        .ring = {0,   17,  121},
    };
}

bool Same(const capfont::Glyph &a, const capfont::Glyph &b) {
    return a.u0 == b.u0 && a.v0 == b.v0 && a.u1 == b.u1 && a.v1 == b.v1;
}

} // namespace

TEST(PaintedGlyph, MadeOnceThenTakenFromTheAtlas) {
    dc::test::GfxFixture fixture(640, 480, 1.0f, dc::test::GfxOptions{.offscreen = true});
    ASSERT_TRUE(capfont::Load(FontFile()));
    capfont::Glyph first;
    capfont::Glyph again;
    ASSERT_TRUE(capfont::GetPaintedGlyph(U'A', -1, 30.0f, 36.0f, Look(), first));
    const unsigned generation = capfont::Generation();
    ASSERT_TRUE(capfont::GetPaintedGlyph(U'A', -1, 30.0f, 36.0f, Look(), again));
    EXPECT_TRUE(Same(first, again));
    EXPECT_EQ(capfont::Generation(), generation);
    // Another size is another glyph.
    ASSERT_TRUE(capfont::GetPaintedGlyph(U'A', -1, 32.0f, 36.0f, Look(), again));
    EXPECT_FALSE(Same(first, again));
}

TEST(PaintedGlyph, EveryFieldOfTheLookIsItsOwnGlyph) {
    dc::test::GfxFixture fixture(640, 480, 1.0f, dc::test::GfxOptions{.offscreen = true});
    ASSERT_TRUE(capfont::Load(FontFile()));
    capfont::Paint glow = Look();
    glow.shadow_alpha = 0.5f;
    glow.shadow_soft = 4.0f;
    const std::vector<capfont::PlacedLetter> word = {
        {.ch = U'E', .x = 0.0f,  .y = 0.0f, .w = 20.0f, .h = 36.0f},
        {.ch = U'Q', .x = 22.0f, .y = 0.0f, .w = 22.0f, .h = 36.0f},
    };
    capfont::Glyph plain;
    capfont::Glyph banded;
    ASSERT_TRUE(capfont::GetPaintedWord(word, glow, plain));
    capfont::Paint band = glow;
    band.band_alpha = 0.5f;
    band.band_y = 18.0f;
    band.band_fade = 8.0f;
    ASSERT_TRUE(capfont::GetPaintedWord(word, band, banded));
    EXPECT_FALSE(Same(plain, banded));
    ASSERT_TRUE(capfont::GetPaintedGlyph(U'E', -1, 20.0f, 36.0f, glow, plain));
    ASSERT_TRUE(capfont::GetPaintedGlyph(U'E', -1, 20.0f, 36.0f, band, banded));
    EXPECT_FALSE(Same(plain, banded));
}

TEST(PaintedGlyph, AGlyphTheFontHasNotIsRefused) {
    dc::test::GfxFixture fixture(640, 480, 1.0f, dc::test::GfxOptions{.offscreen = true});
    ASSERT_TRUE(capfont::Load(FontFile()));
    capfont::Glyph glyph;
    EXPECT_FALSE(capfont::GetPaintedGlyph(U'一', -1, 30.0f, 36.0f, Look(), glyph));
    EXPECT_FALSE(capfont::GetPaintedGlyph(U'一', -1, 30.0f, 36.0f, Look(), glyph));
}

TEST(PaintedGlyph, PaintedAheadIsTakenAsIsNotPaintedAgain) {
    dc::test::GfxFixture fixture(640, 480, 1.0f, dc::test::GfxOptions{.offscreen = true});
    ASSERT_TRUE(capfont::Load(FontFile()));
    const std::vector<capfont::PlacedLetter> word = {{U'N', 0.0f, 0.0f, 20.0f, 30.0f}, {U'O', 22.0f, 1.0f, 21.0f, 29.0f}};
    capfont::Paint                           shadow = Look();
    shadow.shadow_alpha = 0.8f;
    shadow.shadow_soft = 4.0f;
    shadow.shadow_only = true;
    capfont::PaintAhead([&] {
        return std::vector<capfont::PaintJob>{{.word = true, .letters = word, .paint = shadow},
                                              {.word = false, .letters = {{U'N', 0.0f, 0.0f, 20.0f, 30.0f}}, .paint = Look()}};
    });
    capfont::WaitForPaintingAhead();

    namespace profile = platform::load_profile;
    const bool was_active = profile::active;
    profile::active = true;
    const auto     glyphs = static_cast<unsigned>(profile::Kind::Glyphs);
    const auto     painted_before = profile::counters[glyphs].calls;
    capfont::Glyph ahead_word;
    capfont::Glyph ahead_letter;
    ASSERT_TRUE(capfont::GetPaintedWord(word, shadow, ahead_word));
    ASSERT_TRUE(capfont::GetPaintedGlyph(U'N', -1, 20.0f, 30.0f, Look(), ahead_letter));
    EXPECT_EQ(profile::counters[glyphs].calls, painted_before);
    capfont::Glyph other;
    ASSERT_TRUE(capfont::GetPaintedGlyph(U'O', -1, 20.0f, 30.0f, Look(), other));
    EXPECT_EQ(profile::counters[glyphs].calls, painted_before + 1);

    // The same as painting them then: loading the font again forgets the atlas and what was painted ahead.
    ASSERT_TRUE(capfont::Load(FontFile()));
    capfont::Glyph now_word;
    capfont::Glyph now_letter;
    ASSERT_TRUE(capfont::GetPaintedWord(word, shadow, now_word));
    ASSERT_TRUE(capfont::GetPaintedGlyph(U'N', -1, 20.0f, 30.0f, Look(), now_letter));
    EXPECT_EQ(profile::counters[glyphs].calls, painted_before + 3);
    for (const auto &[a, b] : {std::pair{ahead_word, now_word}, std::pair{ahead_letter, now_letter}}) {
        EXPECT_EQ(a.width, b.width);
        EXPECT_EQ(a.height, b.height);
        EXPECT_EQ(a.x_offset, b.x_offset);
        EXPECT_EQ(a.y_offset, b.y_offset);
    }
    profile::active = was_active;
}

TEST(PaintedGlyph, PaintingAgainDoesNotWaitForTheLastAndKeepsNoneOfItsPictures) {
    dc::test::GfxFixture fixture(640, 480, 1.0f, dc::test::GfxOptions{.offscreen = true});
    ASSERT_TRUE(capfont::Load(FontFile()));
    capfont::PaintAhead([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400)); // a picture being painted when it's stopped
        return std::vector<capfont::PaintJob>{{.word = false, .letters = {{U'A', 0.0f, 0.0f, 20.0f, 30.0f}}, .paint = Look()}};
    });
    const auto started = std::chrono::steady_clock::now();
    capfont::PaintAhead([] { return std::vector<capfont::PaintJob>{{.word = false, .letters = {{U'B', 0.0f, 0.0f, 20.0f, 30.0f}}, .paint = Look()}}; });
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::milliseconds(200));
    capfont::WaitForPaintingAhead();

    namespace profile = platform::load_profile;
    const bool was_active = profile::active;
    profile::active = true;
    const auto     glyphs = static_cast<unsigned>(profile::Kind::Glyphs);
    const auto     before = profile::counters[glyphs].calls;
    capfont::Glyph glyph;
    ASSERT_TRUE(capfont::GetPaintedGlyph(U'B', -1, 20.0f, 30.0f, Look(), glyph));
    EXPECT_EQ(profile::counters[glyphs].calls, before);
    ASSERT_TRUE(capfont::GetPaintedGlyph(U'A', -1, 20.0f, 30.0f, Look(), glyph));
    EXPECT_EQ(profile::counters[glyphs].calls, before + 1);
    profile::active = was_active;
}

} // namespace dc::test
