#pragma once

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
DC_RENDERER_CALL(ClearStencil)
DC_RENDERER_CALL(CopyMainDepthToTarget)
DC_RENDERER_CALL(CreateRenderTarget)
DC_RENDERER_CALL(CreateTexture)
DC_RENDERER_CALL(CurrentFrameLayout)
DC_RENDERER_CALL(CurrentRenderTarget)
DC_RENDERER_CALL(CutCameraInterpolation)
DC_RENDERER_CALL(CutInterpolation)
DC_RENDERER_CALL(DepthPlanesAvailable)
DC_RENDERER_CALL(DepthResult)
DC_RENDERER_CALL(DestroyTexture)
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
DC_RENDERER_CALL(NamedRenderTarget)
DC_RENDERER_CALL(ReadDepth)
DC_RENDERER_CALL(Recording)
DC_RENDERER_CALL(SetRenderTarget)
DC_RENDERER_CALL(SnapshotFrame)
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
        MirrorClear(color, rgba, depth, depth_value);
    }
    gfx::Clear(color, rgba, depth, depth_value, rect, presentation_rect);
}

inline void SetUiAnchor(const gfx::UiAnchor &anchor) {
    gfx::SetUiAnchor(anchor);
}

} // namespace dc::render
