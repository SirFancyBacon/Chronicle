#include <gtest/gtest.h>

#include <array>

#include "frame.hpp"
#include "menu_draw.hpp"
#include "menu_inventory.hpp"
#include "renderinfo.hpp"
#include "gfx_fixture.hpp"
#include "render/draw3d.hpp"
#include "render/texture_registry.hpp"
#include "texture.hpp"

namespace dc::test {

using namespace dc::game;
using namespace dc::render;

TEST(ChroniclePresentation, FullSizePictureFillsWidescreenWithoutSamplingPastArtwork) {
    dc::test::GfxFixture fixture(1280, 720, 1.0f, dc::test::GfxOptions{.offscreen = true});
    PortDecodedTexture   image;
    image.width = 640;
    image.height = 448;
    image.levels.emplace_back(640 * 448 * 4, 255);
    for (unsigned y = 0; y < image.height; ++y) {
        for (unsigned x = 0; x < image.width; ++x) {
            const size_t pixel = (static_cast<size_t>(y) * image.width + x) * 4;
            image.levels[0][pixel] = static_cast<uint8_t>(y / 2);
            image.levels[0][pixel + 1] = static_cast<uint8_t>(x / 3);
        }
    }
    const unsigned key = PortCreateTexture(image, PortTextureOwner::Manager, nullptr);
    ASSERT_NE(key, 0u);
    CTexture texture;
    texture.tex0 = SCE_GS_SET_TEX0(key, 10, 0, 10, 9, 1, 0, 0, 0, 0, 0, 0);
    fixture.Frame({0, 0, 0, 128}, [&] { DrawFullSizePicture(&texture, 0, 0, 128); });
    EXPECT_TRUE(fixture.PixelNear(80, 360, 112, 13, 255));
    EXPECT_TRUE(fixture.PixelNear(1200, 360, 112, 200, 255));
    EXPECT_TRUE(fixture.PixelNear(640, 360, 112, 106, 255));
    EXPECT_TRUE(fixture.PixelNear(640, 684, 212, 106, 255));
    PortReleaseKey(key);
}

TEST(ChroniclePresentation, ClearedVisualBlocksAreRejectedBetweenSweeps) {
    alignas(16) std::array<unsigned int, 16> block = {};
    Draw3DRegisterVisual(block.data(), false);
    ASSERT_NE(Draw3DFindVisual(block.data()), nullptr);
    block[8] = 0;
    EXPECT_EQ(Draw3DFindVisual(block.data()), nullptr);
}

} // namespace dc::test
