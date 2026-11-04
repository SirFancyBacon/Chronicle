#pragma once

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

DC_RENDERER_CALL(BeginFrame)
DC_RENDERER_CALL(BeginRecording)
DC_RENDERER_CALL(CanCopyMainDepth)
DC_RENDERER_CALL(CreateTexture)
DC_RENDERER_CALL(CurrentFrameLayout)
DC_RENDERER_CALL(CurrentRenderTarget)
DC_RENDERER_CALL(CutCameraInterpolation)
DC_RENDERER_CALL(CutInterpolation)
DC_RENDERER_CALL(DepthPlanesAvailable)
DC_RENDERER_CALL(DepthResult)
DC_RENDERER_CALL(EndFrame)
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
DC_RENDERER_CALL(ReadDepth)
DC_RENDERER_CALL(Recording)
DC_RENDERER_CALL(UpdateTextureNow)
DC_RENDERER_CALL(VisibleLogicalRect)

#undef DC_RENDERER_CALL

inline void UpdateMeshVertices(gfx::MeshHandle mesh, std::uint32_t first, std::span<const gfx::Vertex3D> vertices) {
    if (MirrorOn()) {
        MirrorUpdateMesh(mesh, first, vertices);
    }
    gfx::UpdateMeshVertices(mesh, first, vertices);
}

inline void Clear(bool color, const std::uint8_t *rgba, bool depth, float depth_value, const gfx::LogicalRect *rect = nullptr,
                  const gfx::LogicalRect *presentation_rect = nullptr) {
    if (MirrorOn()) {
        MirrorClear(color, rgba, depth, depth_value, rect);
    }
    gfx::Clear(color, rgba, depth, depth_value, rect, presentation_rect);
}

inline void ClearStencil(std::uint8_t value, const gfx::LogicalRect *rect = nullptr) {
    if (MirrorOn()) {
        MirrorClearStencil();
    }
    gfx::ClearStencil(value, rect);
}

inline void SetRenderTarget(gfx::TextureHandle target) {
    if (MirrorOn()) {
        MirrorSetTarget(target);
    }
    gfx::SetRenderTarget(target);
}

inline gfx::TextureHandle CreateRenderTarget(std::uint32_t width, std::uint32_t height, bool has_alpha, bool share_main_depth = false, bool frame_target = false) {
    const gfx::TextureHandle target = gfx::CreateRenderTarget(width, height, has_alpha, share_main_depth, frame_target);
    if (MirrorOn()) {
        MirrorSurface(target, width, height, has_alpha, share_main_depth, frame_target);
    }
    return target;
}

inline gfx::TextureHandle NamedRenderTarget(std::string_view name, std::uint32_t width, std::uint32_t height, bool has_alpha, bool share_main_depth = false,
                                            bool frame_target = false) {
    const gfx::TextureHandle before = MirrorOn() ? gfx::FindNamedRenderTarget(name) : gfx::kNullTexture;
    const gfx::TextureHandle target = gfx::NamedRenderTarget(name, width, height, has_alpha, share_main_depth, frame_target);
    if (MirrorOn()) {
        // A name asked for in a new shape is a new target, and the old one is gone.
        if (before != gfx::kNullTexture && before != target) {
            MirrorDestroyTexture(before);
        }
        MirrorSurface(target, width, height, has_alpha, share_main_depth, frame_target);
    }
    return target;
}

inline bool CopyMainDepthToTarget(const gfx::LogicalRect &source) {
    if (!gfx::CopyMainDepthToTarget(source)) {
        return false;
    }
    if (MirrorOn()) {
        MirrorCopyDepth(source);
    }
    return true;
}

inline bool SnapshotFrame(gfx::TextureHandle target) {
    if (MirrorOn()) {
        MirrorSnapshotFrame(target);
    }
    return gfx::SnapshotFrame(target);
}

inline void DestroyTexture(gfx::TextureHandle texture) {
    if (MirrorOn()) {
        MirrorDestroyTexture(texture);
    }
    gfx::DestroyTexture(texture);
}

inline void SetUiAnchor(const gfx::UiAnchor &anchor) {
    gfx::SetUiAnchor(anchor);
}

} // namespace dc::render
