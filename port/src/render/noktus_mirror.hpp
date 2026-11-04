#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "gfx/gfx.hpp"

namespace dc::render {

// DC_NOKTUS_MIRROR=<path>: alongside today's renderer, draws the same 2D through Noktus Nimbus, offscreen, and writes its last picture
// to path at exit. Noktus is built up against today's picture this way while the game keeps running on today's renderer.
// DC_NOKTUS_MOMENT=<0 to 1>: also writes the last tick as drawn that far from the tick before, beside it as <name>.between.png.

/** Whether a mirror was asked for. */
bool MirrorOn();

/** The moment asked for by DC_NOKTUS_MOMENT, if any. */
std::optional<float> MirrorMoment();

void MirrorBeginTick();
void MirrorEndTick();

/** Who the mesh draws from now on belong to, and how they move between ticks. */
void MirrorMotion(gfx::InterpKey key, bool no_interpolation, float teleport_distance, bool blend_vertices);
void MirrorCutMotion();
void MirrorCutCameraMotion();

/** Notes a texture's contents, for the textures Chronicle makes itself. */
void MirrorTexture(gfx::TextureHandle texture, const gfx::TextureDesc &desc);
void MirrorTexturePixels(gfx::TextureHandle texture, std::uint32_t level, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                         std::uint32_t height, const void *pixels);
void MirrorDestroyTexture(gfx::TextureHandle texture);

void MirrorCopyTexture(gfx::TextureHandle from, gfx::Rect rect, gfx::TextureHandle to, std::int32_t x, std::int32_t y);
void MirrorClear(bool color, const std::uint8_t *rgba, bool depth, float depth_value, const gfx::LogicalRect *rect);
void MirrorClearStencil();

void MirrorSurface(gfx::TextureHandle target, std::uint32_t width, std::uint32_t height, bool has_alpha, bool shares_depth, bool frame_target);
void MirrorSetTarget(gfx::TextureHandle target);
void MirrorSnapshotFrame(gfx::TextureHandle target);
void MirrorCopyDepth(const gfx::LogicalRect &source);
void MirrorBlit(gfx::TextureHandle from, gfx::Rect from_rect, gfx::TextureHandle to, gfx::Rect to_rect, gfx::Filter filter);

void MirrorCreateMesh(gfx::MeshHandle mesh, std::span<const gfx::Vertex3D> vertices, std::span<const std::uint32_t> indices);
void MirrorUpdateMesh(gfx::MeshHandle mesh, std::uint32_t first, std::span<const gfx::Vertex3D> vertices);
void MirrorDestroyMesh(gfx::MeshHandle mesh);
void MirrorDrawMesh(gfx::MeshHandle mesh, std::uint32_t first_index, std::uint32_t index_count, const gfx::MeshConstants &constants,
                    const gfx::TextureBinding &texture, const gfx::DrawState &state, const gfx::MeshTransform *transform);
void MirrorDrawImmediate(std::span<const gfx::Vertex3D> vertices, std::span<const std::uint32_t> indices, const gfx::MeshConstants &constants,
                         const gfx::TextureBinding &texture, const gfx::DrawState &state, const gfx::MeshTransform *transform);

void MirrorDraw2D(gfx::Primitive primitive, std::span<const gfx::Vertex2D> vertices, const gfx::TextureBinding &texture,
                  const gfx::DrawState &state);

} // namespace dc::render
