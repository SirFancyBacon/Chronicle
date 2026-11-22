// A fire's hue from the game's art for it (game/firelight.hpp FireHue, render/texture_registry.hpp PortTextureGlow): a texture of fire's colours
// keeps the renderer's own, one of another colour lends it, an indexed one by the palette it's drawn with.
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

#include "firelight.hpp"
#include "render/texture_registry.hpp"
#include "texture.hpp"

namespace {

using namespace dc::game;
using namespace dc::render;

constexpr std::array<float, 3> kNone = {};

std::uint32_t Rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    return static_cast<std::uint32_t>(r) | static_cast<std::uint32_t>(g) << 8 | static_cast<std::uint32_t>(b) << 16 | static_cast<std::uint32_t>(a) << 24;
}

} // namespace

TEST(FireHue, FiresColoursKeepTheRenderersOwn) {
    EXPECT_EQ(FireHue({1.0f, 0.2f, 0.05f}), kNone); // orange
    EXPECT_EQ(FireHue({1.0f, 0.9f, 0.58f}), kNone); // a candle's yellow
    EXPECT_EQ(FireHue({1.0f, 1.0f, 1.0f}), kNone);  // white
    EXPECT_EQ(FireHue({0.97f, 0.98f, 1.0f}), kNone); // near white, blue edging past
}

TEST(FireHue, AnotherColourLendsItsHueBrightestOne) {
    const std::array<float, 3> hue = FireHue({0.4f, 0.2f, 0.36f}); // lavender, as the opening's torches
    EXPECT_FLOAT_EQ(hue[0], 1.0f);
    EXPECT_FLOAT_EQ(hue[1], 0.5f);
    EXPECT_FLOAT_EQ(hue[2], 0.9f);
    EXPECT_EQ(FireHue({0.1f, 0.4f, 0.2f})[1], 1.0f); // green
}

TEST(FireHue, NoLightNoHue) {
    EXPECT_EQ(FireHue({0.0f, 0.0f, 0.0f}), kNone);
    EXPECT_EQ(FireHue({std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f}), kNone);
    EXPECT_EQ(FireHue({0.3f, std::numeric_limits<float>::quiet_NaN(), 0.5f}), kNone);
    EXPECT_EQ(FireHue({std::numeric_limits<float>::infinity(), 0.5f, 0.5f}), kNone);
}

// Texels count by their alpha (and brightness): a clear one adds nothing.
TEST(FireHue, AnRgbaTexturesGlowIsItsBrightTexels) {
    PortDecodedTexture image;
    image.width = 2;
    image.height = 1;
    image.levels.emplace_back();
    for (const std::uint32_t texel : {Rgba(255, 0, 255, 255), Rgba(0, 255, 0, 0)}) {
        for (int byte = 0; byte < 4; byte++) {
            image.levels[0].push_back(static_cast<std::uint8_t>(texel >> (8 * byte)));
        }
    }
    const unsigned key = PortCreateTexture(image, PortTextureOwner::Other, nullptr);
    ASSERT_NE(key, 0u);
    CTexture texture;
    texture.tex0 = SCE_GS_SET_TEX0(key, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0);
    const std::optional<std::array<float, 3>> glow = PortTextureGlow(&texture);
    ASSERT_TRUE(glow.has_value());
    EXPECT_FLOAT_EQ((*glow)[0], 1.0f);
    EXPECT_FLOAT_EQ((*glow)[1], 0.0f); // the clear green texel adds nothing
    EXPECT_FLOAT_EQ((*glow)[2], 1.0f);
    PortReleaseKey(key);
    EXPECT_FALSE(PortTextureGlow(&texture).has_value()) << "released, it's no texture";
    EXPECT_FALSE(PortTextureGlow(nullptr).has_value());
}

// An indexed texture is drawn with the palette CBP names, else its own: its glow follows.
TEST(FireHue, AnIndexedTexturesGlowIsByThePaletteItsDrawnWith) {
    const auto indexed = [](std::uint32_t colour, unsigned *palette) {
        PortDecodedTexture image;
        image.width = 2;
        image.height = 1;
        image.format = gfx::TextureFormat::Index8;
        image.levels.push_back({1, 1});
        image.palette[1] = colour;
        return PortCreateTexture(image, PortTextureOwner::Other, palette);
    };
    unsigned red_palette = 0;
    unsigned blue_palette = 0;
    const unsigned red = indexed(Rgba(255, 0, 0, 255), &red_palette);
    const unsigned blue = indexed(Rgba(0, 0, 255, 255), &blue_palette);
    ASSERT_NE(red, 0u);
    ASSERT_NE(blue_palette, 0u);
    CTexture texture;
    texture.tex0 = SCE_GS_SET_TEX0(red, 1, 0x13, 1, 0, 1, 0, 0, 0, 0, 0, 0);
    std::optional<std::array<float, 3>> glow = PortTextureGlow(&texture);
    ASSERT_TRUE(glow.has_value());
    EXPECT_FLOAT_EQ((*glow)[0], 1.0f) << "its own palette";
    EXPECT_FLOAT_EQ((*glow)[2], 0.0f);
    texture.tex0 = SCE_GS_SET_TEX0(red, 1, 0x13, 1, 0, 1, 0, blue_palette, 0, 0, 0, 0);
    glow = PortTextureGlow(&texture);
    ASSERT_TRUE(glow.has_value());
    EXPECT_FLOAT_EQ((*glow)[0], 0.0f) << "the palette CBP names";
    EXPECT_FLOAT_EQ((*glow)[2], 1.0f);
    PortReleaseKey(red);
    PortReleaseKey(blue);
    PortReleaseKey(red_palette);
    PortReleaseKey(blue_palette);
}
