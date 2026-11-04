#include <gtest/gtest.h>

#include "render/noktus_adapter.hpp"
#include "render/passes.hpp"

namespace dc::test {

using render::DrawsTheSame;
using render::FromMaterial;
using render::ToMaterial;

namespace {

bool SurvivesTheTrip(const gfx::DrawState &state, const gfx::TextureBinding &texture = {}) {
    gfx::DrawState      back;
    gfx::TextureBinding back_texture;
    FromMaterial(ToMaterial(state, texture), back, back_texture);
    return DrawsTheSame(state, texture, back, back_texture);
}

gfx::DrawState Blending(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d, std::uint8_t fix) {
    gfx::DrawState state;
    state.blend = true;
    state.alpha = {a, b, c, d, fix};
    return state;
}

} // namespace

TEST(NoktusAdapter, EachBlendTheGameUsesGetsItsName) {
    EXPECT_EQ(ToMaterial(gfx::DrawState{}, {}).blend, noktus::Blend::Opaque);
    EXPECT_EQ(ToMaterial(Blending(0, 1, 0, 1, 0), {}).blend, noktus::Blend::Alpha);
    EXPECT_EQ(ToMaterial(Blending(0, 2, 0, 1, 0), {}).blend, noktus::Blend::Additive);
    EXPECT_EQ(ToMaterial(Blending(0, 2, 2, 1, 128), {}).blend, noktus::Blend::AddFixed);
    EXPECT_EQ(ToMaterial(Blending(2, 0, 0, 1, 0), {}).blend, noktus::Blend::Subtractive);
    EXPECT_EQ(ToMaterial(Blending(2, 1, 0, 1, 64), {}).blend, noktus::Blend::Darken);
    EXPECT_EQ(ToMaterial(Blending(1, 2, 0, 0, 0), {}).blend, noktus::Blend::Screen);
    EXPECT_EQ(ToMaterial(Blending(2, 2, 2, 1, 0), {}).blend, noktus::Blend::AlphaOnly);
}

TEST(NoktusAdapter, EachBlendSurvivesTheTrip) {
    EXPECT_TRUE(SurvivesTheTrip(Blending(0, 1, 0, 1, 0)));
    EXPECT_TRUE(SurvivesTheTrip(Blending(0, 2, 2, 1, 128)));
    EXPECT_TRUE(SurvivesTheTrip(Blending(2, 0, 0, 1, 0)));
    EXPECT_TRUE(SurvivesTheTrip(Blending(2, 1, 0, 1, 64)));
    EXPECT_TRUE(SurvivesTheTrip(Blending(1, 2, 0, 0, 0)));
    EXPECT_TRUE(SurvivesTheTrip(Blending(2, 2, 2, 1, 0)));
}

TEST(NoktusAdapter, AnAlphaTestThatCantFailIsNoTest) {
    gfx::DrawState state;
    state.alpha_test = true;
    state.alpha_func = gfx::AlphaFunc::GEqual;
    state.alpha_ref = 0;
    EXPECT_EQ(ToMaterial(state, {}).alpha_test, noktus::AlphaTest::Always);
    EXPECT_TRUE(SurvivesTheTrip(state));
    state.alpha_ref = 64;
    EXPECT_EQ(ToMaterial(state, {}).alpha_test, noktus::AlphaTest::GreaterOrEqual);
    EXPECT_TRUE(SurvivesTheTrip(state));
}

TEST(NoktusAdapter, ShadowVolumesAndScissorsSurviveTheTrip) {
    gfx::DrawState count;
    count.color_write_mask = 0;
    count.stencil_test = true;
    count.stencil_front.pass = gfx::StencilOp::IncrementClamp;
    count.stencil_back = count.stencil_front;
    EXPECT_EQ(ToMaterial(count, {}).shadow_volume, noktus::ShadowVolume::Enter);
    EXPECT_TRUE(SurvivesTheTrip(count));

    gfx::DrawState darken;
    darken.stencil_test = true;
    darken.stencil_front = darken.stencil_back = {.compare = gfx::CompareOp::NotEqual, .reference = 0, .compare_mask = 0xFF, .write_mask = 0};
    darken.scissor = true;
    darken.scissor_rect = {1.0f, 2.0f, 3.0f, 4.0f};
    EXPECT_EQ(ToMaterial(darken, {}).shadow_volume, noktus::ShadowVolume::InShadow);
    EXPECT_TRUE(SurvivesTheTrip(darken));
}

// What a material can't say yet must be caught, or the check proves nothing.
TEST(NoktusAdapter, WhatAMaterialCantSayIsCaught) {
    gfx::DrawState unknown_blend = Blending(1, 0, 1, 2, 0);
    EXPECT_FALSE(SurvivesTheTrip(unknown_blend));

    gfx::TextureBinding palette;
    palette.palette = 7;
    EXPECT_FALSE(SurvivesTheTrip({}, palette));

    gfx::DrawState pinned_scissor;
    pinned_scissor.extend_scissor = false;
    EXPECT_FALSE(SurvivesTheTrip(pinned_scissor));

    gfx::DrawState other_stencil;
    other_stencil.stencil_test = true;
    other_stencil.stencil_front.pass = gfx::StencilOp::Invert;
    EXPECT_FALSE(SurvivesTheTrip(other_stencil));
}

TEST(NoktusAdapter, ADrawAMaterialCantSayGoesOnUnchanged) {
    gfx::TextureBinding palette;
    palette.palette = 7;
    const render::Routed routed = render::Route(palette, {});
    EXPECT_EQ(routed.texture.palette, 7u);
}

} // namespace dc::test
