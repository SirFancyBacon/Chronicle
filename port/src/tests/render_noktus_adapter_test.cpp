#include <gtest/gtest.h>

#include <algorithm>

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

namespace {

gfx::MeshConstants LitMesh(const gfx::MeshTransform &transform) {
    gfx::MeshConstants constants = {};
    float              view_model[16];
    std::copy_n(transform.model, 16, view_model);
    std::copy_n(view_model, 16, constants.mvp);
    constants.flags = gfx::kMeshLit | gfx::kMeshFog;
    constants.light_count = 1;
    constants.light_direction[0][1] = -1.0f;
    constants.light_color[0][0] = 0.5f;
    constants.ambient[0] = 0.25f;
    constants.diffuse[0] = constants.diffuse[1] = constants.diffuse[2] = 1.0f;
    constants.diffuse[3] = 0.75f;
    constants.ambient_material[0] = 1.0f;
    constants.fog[1] = 3.0f;
    return constants;
}

} // namespace

TEST(NoktusAdapter, AMeshDrawCanBeSaidInNoktusWords) {
    const gfx::MeshTransform transform = gfx::IdentityMeshTransform();
    const gfx::MeshConstants constants = LitMesh(transform);
    const render::MeshScene  scene = render::ToMeshScene(constants, transform, {}, {});
    EXPECT_EQ(render::MeshSceneDifference(scene, constants), nullptr);
    EXPECT_TRUE(scene.draw.material.lit);
    EXPECT_FLOAT_EQ(scene.draw.opacity, 0.75f); // opacity, apart from any light
    EXPECT_EQ(scene.draw.lighting.light_count, 1);
}

TEST(NoktusAdapter, TheShadowFlagIsOnlyFlatColour) {
    const gfx::MeshTransform transform = gfx::IdentityMeshTransform();
    gfx::MeshConstants       constants = LitMesh(transform);
    constants.flags = gfx::kMeshShadow;
    EXPECT_EQ(render::MeshSceneDifference(render::ToMeshScene(constants, transform, {}, {}), constants), nullptr);
}

// A check that can't fail proves nothing.
TEST(NoktusAdapter, AMeshThatDiffersIsCaught) {
    const gfx::MeshTransform transform = gfx::IdentityMeshTransform();
    const gfx::MeshConstants constants = LitMesh(transform);
    const render::MeshScene  scene = render::ToMeshScene(constants, transform, {}, {});

    gfx::MeshConstants other = constants;
    other.flags |= 1u << 20;
    EXPECT_STREQ(render::MeshSceneDifference(scene, other), "flags");
    other = constants;
    other.ambient[0] = 0.5f;
    EXPECT_STREQ(render::MeshSceneDifference(scene, other), "colours");
    other = constants;
    other.light_color[0][0] = 0.9f;
    EXPECT_STREQ(render::MeshSceneDifference(scene, other), "lights");
    other = constants;
    other.fog[1] = 4.0f;
    EXPECT_STREQ(render::MeshSceneDifference(scene, other), "fog");
    other = constants;
    other.mvp[12] = 5.0f;
    EXPECT_STREQ(render::MeshSceneDifference(scene, other), "matrix");
}

TEST(NoktusAdapter, EveryVertexByteSurvivesTheTrip) {
    for (bool textured : {false, true}) {
        for (int value = 0; value < 256; value++) {
            gfx::Vertex2D vertex = {};
            vertex.x = 1.5f;
            vertex.y = 2.5f;
            vertex.z = 0.25f;
            vertex.u = 3.5f;
            vertex.v = 4.5f;
            for (std::uint8_t &channel : vertex.color) {
                channel = static_cast<std::uint8_t>(value);
            }
            vertex.fog = static_cast<std::uint8_t>(value);
            const gfx::Vertex2D back = render::FromVertex2D(render::ToVertex2D(vertex, textured), textured);
            EXPECT_EQ(back.x, vertex.x);
            EXPECT_EQ(back.y, vertex.y);
            EXPECT_EQ(back.z, vertex.z);
            EXPECT_EQ(back.u, vertex.u);
            EXPECT_EQ(back.v, vertex.v);
            EXPECT_EQ(back.fog, vertex.fog);
            for (int channel = 0; channel < 4; channel++) {
                EXPECT_EQ(back.color[channel], vertex.color[channel]);
            }
        }
    }
}

TEST(NoktusAdapter, ATexturedVertexOf128MultipliesByOne) {
    gfx::Vertex2D vertex = {};
    vertex.color[0] = 128;
    EXPECT_FLOAT_EQ(render::ToVertex2D(vertex, true).color.r, 1.0f);
}

} // namespace dc::test
