#include <gtest/gtest.h>

#include "gfx_fixture.hpp"
#include "render/renderer_calls.hpp"
#include "render/texture_registry.hpp"

namespace {

using namespace dc::render;

// A placeholder's picture is the VRAM it stood for: it goes with the last texture naming it, so that scene after scene doesn't add up.
TEST(NamedTargetLifetime, APlaceholdersTargetGoesWithTheLastKeyNamingIt) {
    dc::test::GfxFixture fixture(1280, 720, 1.0f, dc::test::GfxOptions{.offscreen = true});
    unsigned palette = 0;
    const unsigned first = PortCreatePlaceholder("lifetime_buff", 640, 256, 2, PortTextureOwner::Manager, &palette);
    const unsigned second = PortCreatePlaceholder("lifetime_buff", 640, 256, 2, PortTextureOwner::Manager, &palette);
    ASSERT_NE(gfx::FindNamedRenderTarget("lifetime_buff"), gfx::kNullTexture);

    PortReleaseKey(first);
    EXPECT_NE(gfx::FindNamedRenderTarget("lifetime_buff"), gfx::kNullTexture) << "another key still names it";
    PortReleaseKey(second);
    EXPECT_EQ(gfx::FindNamedRenderTarget("lifetime_buff"), gfx::kNullTexture);
}

// The effects' own targets, asked for by name on each use, go between modes; a held one stays.
TEST(NamedTargetLifetime, BetweenModesOnlyTargetsNoKeyHoldsGo) {
    dc::test::GfxFixture fixture(1280, 720, 1.0f, dc::test::GfxOptions{.offscreen = true});
    ASSERT_NE(NamedRenderTarget("lifetime effect", 640, 480, true), gfx::kNullTexture);
    const unsigned held = PortRegisterNamedTarget("lifetime held", 640, 240, true, PortTextureOwner::Manager);

    PortDropUnheldTargets();
    EXPECT_EQ(gfx::FindNamedRenderTarget("lifetime effect"), gfx::kNullTexture);
    EXPECT_NE(gfx::FindNamedRenderTarget("lifetime held"), gfx::kNullTexture);
    // Asked for again, it's made new.
    EXPECT_NE(NamedRenderTarget("lifetime effect", 640, 480, true), gfx::kNullTexture);
    PortReleaseKey(held);
}

} // namespace
