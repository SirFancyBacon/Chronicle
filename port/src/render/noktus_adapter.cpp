#include "render/noktus_adapter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace dc::render {

namespace {

constexpr float kOne = 128.0f; // the game's value for 1

struct BlendForm {
    noktus::Blend blend;
    std::uint8_t  a;
    std::uint8_t  b;
    std::uint8_t  c;
    std::uint8_t  d;
};

// Each is ((a - b) * c) + d, with a, b and d 0 the surface, 1 what's there, 2 nothing, and c 0 the surface's alpha, 2 a set factor.
constexpr std::array<BlendForm, 7> kBlends = {
    {{noktus::Blend::Alpha, 0, 1, 0, 1},
     {noktus::Blend::Additive, 0, 2, 0, 1},
     {noktus::Blend::AddFixed, 0, 2, 2, 1},
     {noktus::Blend::Subtractive, 2, 0, 0, 1},
     {noktus::Blend::Darken, 2, 1, 0, 1},
     {noktus::Blend::Screen, 1, 2, 0, 0},
     {noktus::Blend::AlphaOnly, 2, 2, 2, 1}}
};

noktus::Color ByteColor(const std::uint8_t *rgb) {
    return {static_cast<float>(rgb[0]) / 255.0f, static_cast<float>(rgb[1]) / 255.0f, static_cast<float>(rgb[2]) / 255.0f, 1.0f};
}

std::uint8_t ToByte(float value, float one) {
    return static_cast<std::uint8_t>(std::lround(value * one));
}

noktus::AlphaTest ToAlphaTest(const gfx::DrawState &state) {
    if (!state.alpha_test) {
        return noktus::AlphaTest::Always;
    }
    switch (state.alpha_func) {
        case gfx::AlphaFunc::Never:
            return noktus::AlphaTest::Never;
        case gfx::AlphaFunc::Always:
            return noktus::AlphaTest::Always;
        case gfx::AlphaFunc::Less:
            return noktus::AlphaTest::Less;
        case gfx::AlphaFunc::LEqual:
            return noktus::AlphaTest::LessOrEqual;
        case gfx::AlphaFunc::Equal:
            return noktus::AlphaTest::Equal;
        case gfx::AlphaFunc::GEqual:
            return state.alpha_ref == 0 ? noktus::AlphaTest::Always : noktus::AlphaTest::GreaterOrEqual;
        case gfx::AlphaFunc::Greater:
            return noktus::AlphaTest::Greater;
        case gfx::AlphaFunc::NotEqual:
            return noktus::AlphaTest::NotEqual;
    }
    return noktus::AlphaTest::Always;
}

gfx::AlphaFunc FromAlphaTest(noktus::AlphaTest test) {
    switch (test) {
        case noktus::AlphaTest::Always:
            return gfx::AlphaFunc::Always;
        case noktus::AlphaTest::Never:
            return gfx::AlphaFunc::Never;
        case noktus::AlphaTest::Less:
            return gfx::AlphaFunc::Less;
        case noktus::AlphaTest::LessOrEqual:
            return gfx::AlphaFunc::LEqual;
        case noktus::AlphaTest::Equal:
            return gfx::AlphaFunc::Equal;
        case noktus::AlphaTest::GreaterOrEqual:
            return gfx::AlphaFunc::GEqual;
        case noktus::AlphaTest::Greater:
            return gfx::AlphaFunc::Greater;
        case noktus::AlphaTest::NotEqual:
            return gfx::AlphaFunc::NotEqual;
    }
    return gfx::AlphaFunc::Always;
}

noktus::DepthTest ToDepthTest(gfx::DepthTest test) {
    switch (test) {
        case gfx::DepthTest::Never:
            return noktus::DepthTest::Never;
        case gfx::DepthTest::Always:
            return noktus::DepthTest::Always;
        case gfx::DepthTest::GEqual:
            return noktus::DepthTest::NearerOrEqual;
        case gfx::DepthTest::Greater:
            return noktus::DepthTest::Nearer;
    }
    return noktus::DepthTest::Always;
}

gfx::DepthTest FromDepthTest(noktus::DepthTest test) {
    switch (test) {
        case noktus::DepthTest::Never:
            return gfx::DepthTest::Never;
        case noktus::DepthTest::Always:
            return gfx::DepthTest::Always;
        case noktus::DepthTest::NearerOrEqual:
            return gfx::DepthTest::GEqual;
        case noktus::DepthTest::Nearer:
            return gfx::DepthTest::Greater;
    }
    return gfx::DepthTest::Always;
}

noktus::ShadowVolume ToShadowVolume(const gfx::DrawState &state) {
    if (!state.stencil_test) {
        return noktus::ShadowVolume::None;
    }
    if (state.stencil_front.compare == gfx::CompareOp::NotEqual) {
        return noktus::ShadowVolume::InShadow;
    }
    return state.stencil_front.pass == gfx::StencilOp::DecrementClamp ? noktus::ShadowVolume::Leave : noktus::ShadowVolume::Enter;
}

// With blending off the blend's settings do nothing, and a factor only counts in the forms that use one.
gfx::GsBlend EffectiveBlend(const gfx::DrawState &state) {
    if (!state.blend) {
        return {};
    }
    gfx::GsBlend blend = state.alpha;
    if (blend.c != 2) {
        blend.fix = 0;
    }
    return blend;
}

} // namespace

noktus::Vertex2D ToVertex2D(const gfx::Vertex2D &vertex, bool textured) {
    // Without a texture a colour is written as it is, but alpha is always a factor, whose 1 is the game's 128.
    const float one = textured ? kOne : 255.0f;
    return {.x = vertex.x,
            .y = vertex.y,
            .depth = vertex.z,
            .u = vertex.u,
            .v = vertex.v,
            .fog = static_cast<float>(vertex.fog) / 255.0f,
            .color = {static_cast<float>(vertex.color[0]) / one, static_cast<float>(vertex.color[1]) / one,
                      static_cast<float>(vertex.color[2]) / one, static_cast<float>(vertex.color[3]) / kOne}};
}

gfx::Vertex2D FromVertex2D(const noktus::Vertex2D &vertex, bool textured) {
    const float   one = textured ? kOne : 255.0f;
    gfx::Vertex2D out = {};
    out.x = vertex.x;
    out.y = vertex.y;
    out.z = vertex.depth;
    out.u = vertex.u;
    out.v = vertex.v;
    out.color[0] = ToByte(vertex.color.r, one);
    out.color[1] = ToByte(vertex.color.g, one);
    out.color[2] = ToByte(vertex.color.b, one);
    out.color[3] = ToByte(vertex.color.a, kOne);
    out.fog = ToByte(vertex.fog, 255.0f);
    return out;
}

noktus::Material ToMaterial(const gfx::DrawState &state, const gfx::TextureBinding &texture) {
    noktus::Material material;
    material.albedo = static_cast<noktus::TextureId>(texture.texture);
    material.filter = texture.filter == gfx::Filter::Nearest ? noktus::Filter::Nearest : noktus::Filter::Linear;
    material.wrap_u = texture.wrap_u == gfx::Wrap::Repeat ? noktus::Wrap::Repeat : noktus::Wrap::Clamp;
    material.wrap_v = texture.wrap_v == gfx::Wrap::Repeat ? noktus::Wrap::Repeat : noktus::Wrap::Clamp;
    material.ignore_texture_alpha = texture.ignore_alpha;
    if (state.blend) {
        for (const BlendForm &form : kBlends) {
            if (form.a == state.alpha.a && form.b == state.alpha.b && form.c == state.alpha.c && form.d == state.alpha.d) {
                material.blend = form.blend;
                material.blend_factor = static_cast<float>(state.alpha.fix) / kOne;
            }
        }
    }
    material.alpha_test = ToAlphaTest(state);
    material.alpha_reference = static_cast<float>(state.alpha_ref) / kOne;
    material.missing_alpha = static_cast<float>(state.texa_ta0) / kOne;
    material.black_is_clear = state.texa_aem;
    material.depth_test = ToDepthTest(state.depth_test);
    material.depth_write = state.depth_write;
    material.cull = state.cull == gfx::CullMode::Back    ? noktus::Cull::Back
                    : state.cull == gfx::CullMode::Front ? noktus::Cull::Front
                                                         : noktus::Cull::None;
    material.fogged = state.fog;
    material.fog_color = ByteColor(state.fog_color);
    material.writes_color = state.color_write_mask != 0;
    material.shadow_volume = ToShadowVolume(state);
    if (state.scissor) {
        material.clip = noktus::Rect{state.scissor_rect.x, state.scissor_rect.y, state.scissor_rect.w, state.scissor_rect.h};
    }
    return material;
}

void FromMaterial(const noktus::Material &material, gfx::DrawState &state, gfx::TextureBinding &texture) {
    state = {};
    texture = {};
    texture.texture = static_cast<gfx::TextureHandle>(material.albedo);
    texture.filter = material.filter == noktus::Filter::Nearest ? gfx::Filter::Nearest : gfx::Filter::Linear;
    texture.wrap_u = material.wrap_u == noktus::Wrap::Repeat ? gfx::Wrap::Repeat : gfx::Wrap::Clamp;
    texture.wrap_v = material.wrap_v == noktus::Wrap::Repeat ? gfx::Wrap::Repeat : gfx::Wrap::Clamp;
    texture.ignore_alpha = material.ignore_texture_alpha;
    for (const BlendForm &form : kBlends) {
        if (form.blend == material.blend) {
            state.blend = true;
            state.alpha = {form.a, form.b, form.c, form.d, ToByte(material.blend_factor, kOne)};
        }
    }
    state.alpha_test = material.alpha_test != noktus::AlphaTest::Always;
    state.alpha_func = FromAlphaTest(material.alpha_test);
    state.alpha_ref = ToByte(material.alpha_reference, kOne);
    state.texa_ta0 = ToByte(material.missing_alpha, kOne);
    state.texa_aem = material.black_is_clear;
    state.depth_test = FromDepthTest(material.depth_test);
    state.depth_write = material.depth_write;
    state.cull = material.cull == noktus::Cull::Back    ? gfx::CullMode::Back
                 : material.cull == noktus::Cull::Front ? gfx::CullMode::Front
                                                        : gfx::CullMode::None;
    state.fog = material.fogged;
    state.fog_color[0] = ToByte(material.fog_color.r, 255.0f);
    state.fog_color[1] = ToByte(material.fog_color.g, 255.0f);
    state.fog_color[2] = ToByte(material.fog_color.b, 255.0f);
    state.color_write_mask = material.writes_color ? gfx::kWriteRgba : 0;
    if (material.shadow_volume == noktus::ShadowVolume::InShadow) {
        state.stencil_test = true;
        state.stencil_front = state.stencil_back = {.compare = gfx::CompareOp::NotEqual, .reference = 0, .compare_mask = 0xFF, .write_mask = 0};
    } else if (material.shadow_volume != noktus::ShadowVolume::None) {
        state.stencil_test = true;
        gfx::StencilFace face;
        face.pass = material.shadow_volume == noktus::ShadowVolume::Leave ? gfx::StencilOp::DecrementClamp : gfx::StencilOp::IncrementClamp;
        state.stencil_front = state.stencil_back = face;
    }
    if (material.clip) {
        state.scissor = true;
        state.scissor_rect = {material.clip->x, material.clip->y, material.clip->w, material.clip->h};
    }
}

namespace {

constexpr std::uint32_t kKnownMeshFlags = gfx::kMeshLit | gfx::kMeshVertexColor | gfx::kMeshFog | gfx::kMeshShadow | gfx::kMeshScreenUv | gfx::kMeshWorldUv;

noktus::Mat4 ToMat4(const float *matrix) {
    noktus::Mat4 out;
    std::copy_n(matrix, 16, out.begin());
    return out;
}

noktus::Mat4 Multiply(const noktus::Mat4 &a, const noktus::Mat4 &b) {
    noktus::Mat4 out{};
    for (int column = 0; column < 4; column++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int i = 0; i < 4; i++) {
                sum += a[static_cast<std::size_t>(i * 4 + row)] * b[static_cast<std::size_t>(column * 4 + i)];
            }
            out[static_cast<std::size_t>(column * 4 + row)] = sum;
        }
    }
    return out;
}

bool SameRgb(const float *a, const noktus::Color &b) {
    return a[0] == b.r && a[1] == b.g && a[2] == b.b;
}

} // namespace

MeshScene ToMeshScene(const gfx::MeshConstants &constants, const gfx::MeshTransform &transform, const gfx::TextureBinding &texture,
                      const gfx::DrawState &state) {
    MeshScene         scene;
    noktus::MeshDraw &draw = scene.draw;
    scene.camera.view = ToMat4(transform.view);
    scene.camera.projection = ToMat4(transform.projection);
    draw.transform = ToMat4(transform.model);
    draw.local = ToMat4(transform.local);
    draw.projection = ToMat4(transform.middle);
    draw.material = ToMaterial(state, texture);
    draw.material.lit = (constants.flags & gfx::kMeshLit) != 0;
    draw.material.fogged = (constants.flags & gfx::kMeshFog) != 0;
    draw.material.tint = {constants.diffuse[0], constants.diffuse[1], constants.diffuse[2], 1.0f};
    draw.ambient_tint = {constants.ambient_material[0], constants.ambient_material[1], constants.ambient_material[2], 1.0f};
    draw.opacity = constants.diffuse[3];
    draw.vertex_colors = (constants.flags & gfx::kMeshVertexColor) != 0;
    draw.uv_source = (constants.flags & gfx::kMeshScreenUv) != 0  ? noktus::UvSource::Screen
                     : (constants.flags & gfx::kMeshWorldUv) != 0 ? noktus::UvSource::World
                                                                  : noktus::UvSource::Mesh;
    std::copy_n(constants.world_uv[0], 4, draw.world_u.begin());
    std::copy_n(constants.world_uv[1], 4, draw.world_v.begin());
    noktus::Lighting &lighting = draw.lighting;
    lighting.ambient = {constants.ambient[0], constants.ambient[1], constants.ambient[2], 1.0f};
    lighting.light_count = static_cast<int>(constants.light_count);
    for (std::size_t i = 0; i < noktus::kMaxDirectionalLights; i++) {
        lighting.lights[i].direction = {constants.light_direction[i][0], constants.light_direction[i][1], constants.light_direction[i][2]};
        lighting.lights[i].color = {constants.light_color[i][0], constants.light_color[i][1], constants.light_color[i][2], 1.0f};
    }
    lighting.fog.offset = constants.fog[0];
    lighting.fog.scale = constants.fog[1];
    lighting.fog.minimum = constants.fog[2];
    lighting.fog.maximum = constants.fog[3];
    return scene;
}

const char *MeshSceneDifference(const MeshScene &scene, const gfx::MeshConstants &constants) {
    const noktus::MeshDraw &draw = scene.draw;
    const noktus::Lighting &lighting = draw.lighting;
    std::uint32_t           flags = (draw.material.lit ? gfx::kMeshLit : 0u) | (draw.vertex_colors ? gfx::kMeshVertexColor : 0u) |
                          (draw.material.fogged ? gfx::kMeshFog : 0u) |
                          (draw.uv_source == noktus::UvSource::Screen  ? gfx::kMeshScreenUv
                           : draw.uv_source == noktus::UvSource::World ? gfx::kMeshWorldUv
                                                                       : 0u);
    // The shadow flag only skips lighting and vertex colours, which a mesh with neither skips anyway.
    const std::uint32_t given = constants.flags & ~static_cast<std::uint32_t>(gfx::kMeshShadow);
    if (flags != (given & kKnownMeshFlags) || (given & ~kKnownMeshFlags) != 0) {
        return "flags";
    }
    if (!SameRgb(constants.diffuse, draw.material.tint) || !SameRgb(constants.ambient_material, draw.ambient_tint) ||
        constants.diffuse[3] != draw.opacity || !SameRgb(constants.ambient, lighting.ambient) ||
        static_cast<int>(constants.light_count) != lighting.light_count) {
        return "colours";
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(lighting.light_count); i++) {
        const noktus::Vec3 &direction = lighting.lights[i].direction;
        if (constants.light_direction[i][0] != direction.x || constants.light_direction[i][1] != direction.y ||
            constants.light_direction[i][2] != direction.z || !SameRgb(constants.light_color[i], lighting.lights[i].color)) {
            return "lights";
        }
    }
    if (constants.fog[0] != lighting.fog.offset || constants.fog[1] != lighting.fog.scale || constants.fog[2] != lighting.fog.minimum ||
        constants.fog[3] != lighting.fog.maximum) {
        return "fog";
    }
    const noktus::Mat4 mvp = Multiply(Multiply(Multiply(Multiply(scene.camera.projection, scene.camera.view), draw.projection), draw.transform), draw.local);
    float              largest = 0.0f;
    for (float value : constants.mvp) {
        largest = std::max(largest, std::abs(value));
    }
    for (std::size_t i = 0; i < 16; i++) {
        // Shadow volumes multiply a matrix by its own inverse, which rounds further than the rest.
        if (std::abs(mvp[i] - constants.mvp[i]) > largest * 1e-3f) {
            return "matrix";
        }
    }
    return nullptr;
}

bool DrawsTheSame(const gfx::DrawState &a, const gfx::TextureBinding &a_texture, const gfx::DrawState &b,
                  const gfx::TextureBinding &b_texture) {
    gfx::DrawState left = a;
    gfx::DrawState right = b;
    for (gfx::DrawState *state : {&left, &right}) {
        state->alpha = EffectiveBlend(*state);
        const noktus::AlphaTest test = ToAlphaTest(*state);
        state->alpha_test = test != noktus::AlphaTest::Always;
        state->alpha_func = state->alpha_test ? state->alpha_func : gfx::AlphaFunc::Always;
        state->alpha_ref = state->alpha_test ? state->alpha_ref : 0;
        if (!state->scissor) {
            state->scissor_rect = {};
        }
        if (!state->stencil_test) {
            state->stencil_front = state->stencil_back = {};
        }
    }
    return left == right && a_texture == b_texture;
}

} // namespace dc::render
