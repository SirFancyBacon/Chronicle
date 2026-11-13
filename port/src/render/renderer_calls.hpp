#pragma once

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gfx/gfx.hpp"
#include "render/noktus_mirror.hpp"

namespace gfx {
/** The names render targets are kept under: Chronicle's host keeps them while Noktus draws; none under the old renderer, which keeps its own. */
std::vector<std::string> NamedRenderTargetNames();
} // namespace gfx

// The renderer calls game code makes, other than drawing (render/passes.hpp). Game code goes through these rather than gfx, so
// that there is one place to move when the renderer changes. tools/check_boundary.sh in Noktus Nimbus keeps it that way.

namespace dc::render {

/** Depth of field and heat haze, in the game's settings turned into distances: see noktus::Focus. */
struct FocusSettings {
    float near = 0.0f;
    float near_band = 0.0f;
    float near_blur = 0.0f;
    float near_spread = 0.0f; // a share of the frame's height
    float far = 0.0f;
    float far_band = 0.0f;
    float far_blur = 0.0f;
    float far_spread = 0.0f;
    float haze = 0.0f; // the most bend, as a share of the frame's height
    float haze_start = 0.0f;
    float haze_end = 0.0f;
};

/** Whether the renderer draws depth of field and heat haze itself (ApplyFocus), in place of the game's layered copies: whenever Noktus draws. The
 * copies remain for the builds Kazarov draws. */
bool ModernFocus();

/** Whether the game's light becomes modern light: Noktus drawing the scene in linear light. The towns' sun and moon then follow their arcs. */
bool ModernLight();

/** Whether the scene is outdoors, its ambient coming more from the sky above than the ground below: set by the towns' light each tick, cleared as
 * any mode starts and in an interior. Indoors, in dungeons and on the title, the ambient falls alike from every side, as the game's did. */
void SetOutdoorLight(bool outdoors);

/** Whether the scene is outdoors (SetOutdoorLight). */
bool OutdoorLight();

/** Lays depth of field and heat haze on the screen as drawn so far, where the game drew its own. */
void ApplyFocus(const FocusSettings &settings);


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
    if (KazarovDraws()) {
        gfx::ReadDepth(id, x, y, width, height);
    }
}

inline std::optional<float> DepthResult(std::uint32_t id) {
    if (!KazarovDraws()) {
        return MirrorDepthResult(id);
    }
    const std::optional<float> depth = gfx::DepthResult(id);
    if (depth) {
        MirrorCompareDepth(id, *depth);
    }
    return depth;
}

inline void CutInterpolation() {
    MirrorCutMotion();
    gfx::CutInterpolation();
}

inline void CutCameraInterpolation() {
    MirrorCutCameraMotion();
    gfx::CutCameraInterpolation();
}

inline void SetInterpKey(gfx::InterpKey key, bool no_interpolation = false, float teleport_distance = std::numeric_limits<float>::infinity(), bool blend_vertices = false) {
    MirrorMotion(key, no_interpolation, teleport_distance, blend_vertices);
    gfx::SetInterpKey(key, no_interpolation, teleport_distance, blend_vertices);
}

inline void UpdateMeshVertices(gfx::MeshHandle mesh, std::uint32_t first, std::span<const gfx::Vertex3D> vertices) {
    MirrorUpdateMesh(mesh, first, vertices);
    gfx::UpdateMeshVertices(mesh, first, vertices);
}

inline void Clear(bool color, const std::uint8_t *rgba, bool depth, float depth_value, const gfx::LogicalRect *rect = nullptr, const gfx::LogicalRect *presentation_rect = nullptr) {
    MirrorClear(color, rgba, depth, depth_value, rect);
    if (KazarovDraws()) {
        gfx::Clear(color, rgba, depth, depth_value, rect, presentation_rect);
    }
}

inline void ClearStencil(std::uint8_t value, const gfx::LogicalRect *rect = nullptr) {
    MirrorClearStencil();
    if (KazarovDraws()) {
        gfx::ClearStencil(value, rect);
    }
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
    if (!KazarovDraws()) {
        return MirrorCopyDepth(source);
    }
    if (!gfx::CopyMainDepthToTarget(source)) {
        return false;
    }
    MirrorCopyDepth(source);
    return true;
}

inline bool SnapshotFrame(gfx::TextureHandle target) {
    MirrorSnapshotFrame(target);
    return !KazarovDraws() || gfx::SnapshotFrame(target);
}

inline void DestroyTexture(gfx::TextureHandle texture) {
    MirrorDestroyTexture(texture);
    gfx::DestroyTexture(texture);
}

/** Lets go of the render target kept under this name, if there is one: the next asked for by it is made new, black. */
inline void DropNamedRenderTarget(std::string_view name) {
    const gfx::TextureHandle target = gfx::FindNamedRenderTarget(name);
    if (target != gfx::kNullTexture) {
        DestroyTexture(target);
    }
}

// What HDR is sending now, mode Off when it isn't, and the patches shown over the picture for calibrating it.
inline HdrOutput HdrShown() {
    return MirrorHdrShown();
}

inline void ShowHdrPatches(std::span<const MirrorHdrPatch> patches) {
    MirrorHdrPatches(patches);
}

inline void SetUiAnchor(const gfx::UiAnchor &anchor) {
    gfx::SetUiAnchor(anchor);
}

} // namespace dc::render
