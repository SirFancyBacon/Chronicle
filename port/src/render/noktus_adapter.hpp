#pragma once

#include "gfx/gfx.hpp"
#include "noktus/scene.hpp"

namespace dc::render {

// The one place that knows both the game's drawing state and Noktus's words for it.

/** The material a draw's state and texture come to. */
noktus::Material ToMaterial(const gfx::DrawState &state, const gfx::TextureBinding &texture);

/** The state and texture that draw a material on today's renderer. */
void FromMaterial(const noktus::Material &material, gfx::DrawState &state, gfx::TextureBinding &texture);

/** Whether two states draw the same, leaving out what can't matter: a blend's settings with blending off, or an alpha test that can't fail. */
bool DrawsTheSame(const gfx::DrawState &a, const gfx::TextureBinding &a_texture, const gfx::DrawState &b,
                  const gfx::TextureBinding &b_texture);

/** What a mesh draw comes to in Noktus's words. */
struct MeshScene {
    noktus::Camera   camera;
    noktus::MeshDraw draw;
};

/** The camera and draw a mesh draw's constants and transform come to. */
MeshScene ToMeshScene(const gfx::MeshConstants &constants, const gfx::MeshTransform &transform, const gfx::TextureBinding &texture,
                      const gfx::DrawState &state);

/** What can't be got back from what ToMeshScene made of a mesh draw's constants, the matrices to within rounding, or nullptr. */
const char *MeshSceneDifference(const MeshScene &scene, const gfx::MeshConstants &constants);

} // namespace dc::render
