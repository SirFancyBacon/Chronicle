#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include "gfx_fixture.hpp"
#include "platform/window.hpp"

namespace dc::test {

namespace {

constexpr float kWide = 1.5f; // 720 rows over the frame's 480

} // namespace

TEST(GfxHost, TheFrameLiesInTheMiddleOfAWideWindow) {
    GfxFixture                fixture(1280, 720);
    const gfx::LogicalMapping mapping = gfx::GetLogicalMapping(gfx::kMainTarget);
    EXPECT_EQ(mapping.scale_x, kWide);
    EXPECT_EQ(mapping.scale_y, kWide);
    EXPECT_EQ(mapping.offset_x, 160.0f);
    EXPECT_EQ(mapping.offset_y, 0.0f);
    EXPECT_EQ(mapping.pixel_width, 1280u);
}

TEST(GfxHost, FillingTheWindowShowsMoreThanTheFrame) {
    GfxFixture fixture(1280, 720);
    gfx::SetFrameLayout({.aspect = gfx::AspectMode::Fill});
    const gfx::LogicalRect visible = gfx::VisibleLogicalRect(gfx::kMainTarget);
    EXPECT_FLOAT_EQ(visible.x, -160.0f / kWide);
    EXPECT_FLOAT_EQ(visible.w, 1280.0f / kWide);
    gfx::SetFrameLayout({.aspect = gfx::AspectMode::Letterbox});
    const gfx::LogicalRect frame = gfx::VisibleLogicalRect(gfx::kMainTarget);
    EXPECT_EQ(frame.x, 0.0f);
    EXPECT_EQ(frame.w, 640.0f);
}

TEST(GfxHost, TheInterfaceScalesAboutTheMiddle) {
    GfxFixture fixture(1280, 720);
    gfx::SetFrameLayout({.aspect = gfx::AspectMode::Fill, .ui_scale = 0.5f});
    const gfx::LogicalMapping mapping = gfx::GetUiMapping(gfx::kMainTarget);
    EXPECT_EQ(mapping.scale_x, kWide * 0.5f);
    EXPECT_EQ(mapping.offset_x, 400.0f);
    EXPECT_EQ(mapping.offset_y, 180.0f);
}

TEST(GfxHost, FrameTargetsFollowTheLayout) {
    GfxFixture fixture(1280, 720);
    gfx::SetFrameLayout({.aspect = gfx::AspectMode::Fill});
    const gfx::TextureHandle whole = gfx::CreateRenderTarget(640, 480, true, false, true);
    const gfx::TextureHandle half = gfx::CreateRenderTarget(640, 224, true, false, true);
    EXPECT_EQ(gfx::GetTextureInfo(whole)->pixel_width, 1280u);
    EXPECT_EQ(gfx::GetTextureInfo(whole)->pixel_height, 720u);
    EXPECT_EQ(gfx::GetTextureInfo(half)->pixel_width, 1280u);
    EXPECT_EQ(gfx::GetTextureInfo(half)->pixel_height, 336u);
    gfx::SetFrameLayout({.aspect = gfx::AspectMode::Letterbox});
    EXPECT_EQ(gfx::GetTextureInfo(half)->pixel_width, 640u);
    EXPECT_EQ(gfx::GetTextureInfo(half)->pixel_height, 224u);
}

TEST(GfxHost, TargetsFollowTheWindow) {
    GfxFixture fixture(1280, 720);
    gfx::SetFrameLayout({.aspect = gfx::AspectMode::Fill});
    const gfx::TextureHandle whole = gfx::CreateRenderTarget(640, 480, true, false, true);
    ASSERT_TRUE(SDL_SetWindowSize(WindowHandle(), 1920, 1080));
    SDL_SyncWindow(WindowHandle());
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(WindowHandle(), &width, &height);
    if (width != 1920) {
        GTEST_SKIP() << "this video driver doesn't resize";
    }
    EXPECT_EQ(gfx::GetTextureInfo(whole)->pixel_width, 1920u);
    EXPECT_EQ(gfx::GetTextureInfo(whole)->pixel_height, 1080u);
}

TEST(GfxHost, HandlesStayClearOfTheReservedOnes) {
    GfxFixture fixture;
    const gfx::TextureHandle texture = gfx::CreateTexture({1, 1});
    EXPECT_NE(texture, gfx::kNullTexture);
    EXPECT_NE(texture, gfx::kMainTarget);
    EXPECT_NE(texture, gfx::kPreviousFrame);
}

TEST(GfxHost, ANamedTargetIsKeptUntilItChangesShape) {
    GfxFixture fixture;
    const gfx::TextureHandle first = gfx::NamedRenderTarget("x", 64, 64, true);
    EXPECT_EQ(gfx::NamedRenderTarget("x", 64, 64, true), first);
    EXPECT_EQ(gfx::FindNamedRenderTarget("x"), first);
    const gfx::TextureHandle second = gfx::NamedRenderTarget("x", 128, 64, true);
    EXPECT_NE(second, first);
    EXPECT_FALSE(gfx::GetTextureInfo(first).has_value());
}

TEST(GfxHost, ATextureThatCantBeMadeIsRefused) {
    GfxFixture fixture;
    EXPECT_EQ(gfx::CreateTexture({0, 4}), gfx::kNullTexture);
    EXPECT_EQ(gfx::CreateTexture({1u << 20, 4}), gfx::kNullTexture);
    gfx::TextureDesc too_many_levels = {2, 2};
    too_many_levels.mip_levels = 3;
    EXPECT_EQ(gfx::CreateTexture(too_many_levels), gfx::kNullTexture);
}

} // namespace dc::test
