#pragma once

#include <cstdint>
#include <span>

#include "gfx/gfx.hpp"

namespace dc::render {

// DC_NOKTUS_MIRROR=<path>: alongside today's renderer, draws the same 2D through Noktus Nimbus, offscreen, and writes its last picture
// to path at exit. Noktus is built up against today's picture this way while the game keeps running on today's renderer.

/** Whether a mirror was asked for. */
bool MirrorOn();

void MirrorBeginTick();
void MirrorEndTick();

/** Notes a texture's contents, for the textures Chronicle makes itself. */
void MirrorTexture(gfx::TextureHandle texture, const gfx::TextureDesc &desc);
void MirrorTexturePixels(gfx::TextureHandle texture, std::uint32_t level, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                         std::uint32_t height, const void *pixels);
void MirrorDestroyTexture(gfx::TextureHandle texture);

void MirrorDraw2D(gfx::Primitive primitive, std::span<const gfx::Vertex2D> vertices, const gfx::TextureBinding &texture,
                  const gfx::DrawState &state);

} // namespace dc::render
