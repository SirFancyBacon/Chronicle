#pragma once

#include "gfx/gfx.hpp"
#include "noktus/material.hpp"

namespace dc::render {

// The one place that knows both the game's drawing state and Noktus's words for it.

/** The material a draw's state and texture come to. */
noktus::Material ToMaterial(const gfx::DrawState &state, const gfx::TextureBinding &texture);

/** The state and texture that draw a material on today's renderer. */
void FromMaterial(const noktus::Material &material, gfx::DrawState &state, gfx::TextureBinding &texture);

/** Whether two states draw the same, leaving out what can't matter: a blend's settings with blending off, or an alpha test that can't fail. */
bool DrawsTheSame(const gfx::DrawState &a, const gfx::TextureBinding &a_texture, const gfx::DrawState &b,
                  const gfx::TextureBinding &b_texture);

} // namespace dc::render
