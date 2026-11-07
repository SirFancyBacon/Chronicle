#pragma once

#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include "gfx/gfx.hpp"
#include "render/noktus_mirror.hpp"

// The renderer calls game code makes, other than drawing (render/passes.hpp). Game code goes through these rather than gfx, so
// that there is one place to move when the renderer changes. tools/check_boundary.sh in Noktus Nimbus keeps it that way.

namespace dc::render {

#define DC_RENDERER_CALL(name)                                    \
    template <typename... Args>                                   \
    decltype(auto) name(Args &&...args) {                         \
        return gfx::name(std::forward<Args>(args)...);            \
    }

DC_RENDERER_CALL(BeginRecording)
DC_RENDERER_CALL(CanCopyMainDepth)
DC_RENDERER_CALL(CurrentFrameLayout)
DC_RENDERER_CALL(CurrentRenderTarget)
DC_RENDERER_CALL(DepthPlanesAvailable)
DC_RENDERER_CALL(EndRecording)
DC_RENDERER_CALL(FindNamedRenderTarget)
DC_RENDERER_CALL(GetLogicalMapping)
DC_RENDERER_CALL(GetTextureInfo)
DC_RENDERER_CALL(GetUiMapping)
DC_RENDERER_CALL(IdentityMeshTransform)
DC_RENDERER_CALL(InFrame)
DC_RENDERER_CALL(InvertAffineTransform)
DC_RENDERER_CALL(LogicalFrame)
DC_RENDERER_CALL(LogicalHeight)
DC_RENDERER_CALL(LogicalWidth)
DC_RENDERER_CALL(Recording)
DC_RENDERER_CALL(VisibleLogicalRect)

#undef DC_RENDERER_CALL

// A frame outside the game's ticks, as the loading screen draws: to Noktus it's a tick like any other.
inline bool BeginFrame() {
    const bool open = gfx::BeginFrame();
    if (open) {
        MirrorBeginTick();
    }
    return open;
}

inline void EndFrame() {
    MirrorEndTick();
    gfx::EndFrame();
}

inline gfx::TextureHandle CreateTexture(const gfx::TextureDesc &desc) {
    const gfx::TextureHandle texture = gfx::CreateTexture(desc);
    if (texture != gfx::kNullTexture) {
        MirrorTexture(texture, desc);
    }
    return texture;
}

inline bool UpdateTexture(gfx::TextureHandle texture, std::uint32_t mip, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, const void *pixels) {
    MirrorTexturePixels(texture, mip, x, y, width, height, pixels);
    return gfx::UpdateTexture(texture, mip, x, y, width, height, pixels);
}

inline bool UpdatePalette(gfx::TextureHandle palette, const std::uint32_t *colors, std::uint32_t first = 0, std::uint32_t count = 256) {
    MirrorPalette(palette, colors, first, count);
    return gfx::UpdatePalette(palette, colors, first, count);
}

inline bool UpdateTextureNow(gfx::TextureHandle texture, std::uint32_t mip, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, const void *pixels) {
    MirrorTexturePixels(texture, mip, x, y, width, height, pixels);
    return gfx::UpdateTextureNow(texture, mip, x, y, width, height, pixels);
}

inline void ReadDepth(std::uint32_t id, float x, float y, float width, float height) {
    MirrorReadDepth(id, {x, y, width, height});
}

inline std::optional<float> DepthResult(std::uint32_t id) {
    return MirrorDepthResult(id);
}

inline void CutInterpolation() {
    MirrorCutMotion();
}

inline void CutCameraInterpolation() {
    MirrorCutCameraMotion();
}

inline void SetInterpKey(gfx::InterpKey key, bool no_interpolation = false, float teleport_distance = std::numeric_limits<float>::infinity(), bool blend_vertices = false) {
    MirrorMotion(key, no_interpolation, teleport_distance, blend_vertices);
    gfx::SetInterpKey(key, no_interpolation, teleport_distance, blend_vertices);
}

inline void UpdateMeshVertices(gfx::MeshHandle mesh, std::uint32_t first, std::span<const gfx::Vertex3D> vertices) {
    MirrorUpdateMesh(mesh, first, vertices);
}

inline void Clear(bool color, const std::uint8_t *rgba, bool depth, float depth_value, const gfx::LogicalRect *rect = nullptr, const gfx::LogicalRect * /*presentation_rect*/ = nullptr) {
    MirrorClear(color, rgba, depth, depth_value, rect);
}

inline void ClearStencil(std::uint8_t /*value*/, const gfx::LogicalRect * /*rect*/ = nullptr) {
    MirrorClearStencil();
}

inline void SetRenderTarget(gfx::TextureHandle target) {
    MirrorSetTarget(target);
    gfx::SetRenderTarget(target);
}

inline gfx::TextureHandle CreateRenderTarget(std::uint32_t width, std::uint32_t height, bool has_alpha, bool share_main_depth = false, bool frame_target = false) {
    const gfx::TextureHandle target = gfx::CreateRenderTarget(width, height, has_alpha, share_main_depth, frame_target);
    MirrorSurface(target, width, height, has_alpha, share_main_depth, frame_target);
    return target;
}

inline gfx::TextureHandle NamedRenderTarget(std::string_view name, std::uint32_t width, std::uint32_t height, bool has_alpha, bool share_main_depth = false,
                                            bool frame_target = false) {
    const gfx::TextureHandle before = gfx::FindNamedRenderTarget(name);
    const gfx::TextureHandle target = gfx::NamedRenderTarget(name, width, height, has_alpha, share_main_depth, frame_target);
    // A name asked for in a new shape is a new target, and the old one is gone.
    if (before != gfx::kNullTexture && before != target) {
        MirrorDestroyTexture(before);
    }
    MirrorSurface(target, width, height, has_alpha, share_main_depth, frame_target);
    return target;
}

inline bool CopyMainDepthToTarget(const gfx::LogicalRect &source) {
    return MirrorCopyDepth(source);
}

inline bool SnapshotFrame(gfx::TextureHandle target) {
    MirrorSnapshotFrame(target);
    return true;
}

inline void DestroyTexture(gfx::TextureHandle texture) {
    MirrorDestroyTexture(texture);
    gfx::DestroyTexture(texture);
}

inline void SetUiAnchor(const gfx::UiAnchor &anchor) {
    gfx::SetUiAnchor(anchor);
}

} // namespace dc::render
