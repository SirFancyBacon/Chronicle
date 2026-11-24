#include <gtest/gtest.h>

#include <array>

#include "gfx_fixture.hpp"
#include "render/draw2d.hpp"
#include "render/gs_state.hpp"
#include "render/passes.hpp"
#include "render/texture_registry.hpp"
#include "ui/framework/widgets.hpp"

// A picture over the whole frame kept 4:3 in a wide window (ui::Picture::DrawSides, render::FramePictureScope): its sides filled from its own edge
// columns, and the picture keeping to the frame whatever the Interface Size.
namespace dc::test {

using namespace dc::render;

namespace {

constexpr u_char kGrey = 100;

// A 640 by 448 picture, as an event's page is: white down its left column, black down its right, grey between.
unsigned EdgedPicture() {
    PortDecodedTexture image;
    image.width = 640;
    image.height = 448;
    image.levels.emplace_back(static_cast<size_t>(640) * 448 * 4, 255);
    for (unsigned y = 0; y < image.height; ++y) {
        for (unsigned x = 0; x < image.width; ++x) {
            const size_t  pixel = (static_cast<size_t>(y) * image.width + x) * 4;
            const uint8_t value = x == 0 ? 255 : x == image.width - 1 ? 0 : kGrey;
            image.levels[0][pixel] = image.levels[0][pixel + 1] = image.levels[0][pixel + 2] = value;
        }
    }
    return PortCreateTexture(image, PortTextureOwner::Manager, nullptr);
}

u_long Tex0(unsigned key) {
    return SCE_GS_SET_TEX0(key, 10, 0, 10, 9, 1, 0, 0, 0, 0, 0, 0);
}

// As the event's sprite table draws it: its sides, then the picture over the frame, kept 4:3 and to the frame, at this alpha (0x80 opaque).
void DrawPage(unsigned key, u_char alpha = 128) {
    Layer2DScope      layer(Layer2D::Ui, Layer2DScope::kDiffersFromRule);
    FramePictureScope frame_picture;
    gfx::DrawState    state = draw2d::SpriteState();
    sceGsAlpha        blend{};
    blend.bits.a = 0; // (picture - screen) * alpha + screen: the game's blend, which the table draws with
    blend.bits.b = 1;
    blend.bits.c = 0;
    blend.bits.d = 1;
    MGPortApplyAlpha(state, blend);
    ui::Picture::DrawSides(Tex0(key), 1, state, ui::Rect(0, 0, 640, 480), ui::Rect(0, 0, 640, 448), 128, 128, 128, alpha);
    std::array<gfx::Vertex2D, 4> quad = {
        draw2d::Vertex(0, 0, 0, 0, 0, 128, 128, 128, alpha),
        draw2d::Vertex(640, 0, 0, 640, 0, 128, 128, 128, alpha),
        draw2d::Vertex(640, 480, 0, 640, 448, 128, 128, 128, alpha),
        draw2d::Vertex(0, 480, 0, 0, 448, 128, 128, 128, alpha),
    };
    draw2d::DrawTextured(gfx::Primitive::Quads, quad, Tex0(key), 1, state);
    draw2d::RestoreTestZbuf();
}

} // namespace

TEST(PictureSides, AWideWindowsSidesTakeThePicturesOwnEdges) {
    GfxFixture fixture(1280, 720);
    gfx::SetFrameLayout({.aspect = gfx::AspectMode::Fill});
    const unsigned key = EdgedPicture();
    ASSERT_NE(key, 0u);
    fixture.Frame({0, 0, 128, 128}, [&] { DrawPage(key); });
    // The frame is 960 wide in the middle of 1280: 160 each side.
    EXPECT_TRUE(fixture.PixelNear(20, 360, 255, 255, 255)) << "the left side is the picture's white left edge";
    EXPECT_TRUE(fixture.PixelNear(150, 100, 255, 255, 255));
    EXPECT_TRUE(fixture.PixelNear(1260, 360, 0, 0, 0)) << "the right side is its black right edge";
    EXPECT_TRUE(fixture.PixelNear(1130, 600, 0, 0, 0));
    EXPECT_TRUE(fixture.PixelNear(640, 360, kGrey, kGrey, kGrey)) << "and the picture itself stays 4:3";
    EXPECT_TRUE(fixture.PixelNear(170, 360, kGrey, kGrey, kGrey));
    PortReleaseKey(key);
}

TEST(PictureSides, APictureOfTheFrameKeepsToItWhateverTheInterfaceSize) {
    GfxFixture fixture(1280, 720);
    gfx::SetFrameLayout({.aspect = gfx::AspectMode::Fill, .ui_scale = 0.5f});
    const unsigned key = EdgedPicture();
    ASSERT_NE(key, 0u);
    fixture.Frame({0, 0, 128, 128}, [&] { DrawPage(key); });
    // At half size the interface would leave the frame's corners blue.
    EXPECT_TRUE(fixture.PixelNear(170, 10, kGrey, kGrey, kGrey));
    EXPECT_TRUE(fixture.PixelNear(1110, 710, kGrey, kGrey, kGrey));
    EXPECT_TRUE(fixture.PixelNear(20, 10, 255, 255, 255));
    EXPECT_TRUE(fixture.PixelNear(1260, 710, 0, 0, 0));
    PortReleaseKey(key);
}

TEST(PictureSides, APictureFadingInHasNoSeamAtTheFramesEdge) {
    GfxFixture fixture(1280, 720);
    gfx::SetFrameLayout({.aspect = gfx::AspectMode::Fill});
    const unsigned key = EdgedPicture();
    ASSERT_NE(key, 0u);
    fixture.Frame({0, 0, 128, 128}, [&] { DrawPage(key, 64); });
    // Half over blue: the side half white, the picture's own white column at the frame's edge (x 160 to 161) the same, not blended twice.
    const std::array<uint8_t, 4> side = fixture.Pixel(150, 360);
    const std::array<uint8_t, 4> edge = fixture.Pixel(160, 360);
    EXPECT_NEAR(side[0], 128, 3);
    EXPECT_NEAR(edge[0], side[0], 2) << "the picture's edge column, under which no side was drawn";
    EXPECT_NEAR(edge[2], side[2], 2);
    PortReleaseKey(key);
}

TEST(PictureSides, AWindowNoWiderThanTheFrameHasNoSides) {
    GfxFixture fixture(640, 480);
    gfx::SetFrameLayout({.aspect = gfx::AspectMode::Fill});
    const unsigned key = EdgedPicture();
    ASSERT_NE(key, 0u);
    fixture.Frame({0, 0, 128, 128}, [&] {
        Layer2DScope layer(Layer2D::Ui, Layer2DScope::kDiffersFromRule);
        ui::Picture::DrawSides(Tex0(key), 1, draw2d::SpriteState(), ui::Rect(0, 0, 640, 480), ui::Rect(0, 0, 640, 448), 128, 128, 128, 128);
    });
    EXPECT_TRUE(fixture.PixelNear(0, 240, 0, 0, 128)) << "nothing drawn, not even the strip under the picture's edge";
    EXPECT_TRUE(fixture.PixelNear(639, 240, 0, 0, 128));
    PortReleaseKey(key);
}

} // namespace dc::test
