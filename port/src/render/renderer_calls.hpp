#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
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

/** A small light with a place in the world, as a fire or a lamp is: see AddPointLight. */
struct PointLightGiven {
    std::uint64_t key = 0;       // who this is from tick to tick, so it moves between ticks: one light keeps one key, 0 for none
    float         position[3] = {};
    float         color[3] = {}; // its intensity in linear light, as the light it gives a surface facing it squarely one unit away
    float         range = 0.0f;  // past this distance it gives nothing
    float         size = 1.0f;   // its own size: nearer than this, it gives no more than there
};

/** Adds a point light to this tick's scene, lighting the world's lit meshes near it with modern light (ModernLight); nothing otherwise. */
void AddPointLight(const PointLightGiven &light);

/** The bright heart of a flame or lamp, seen: see AddGlow. */
struct GlowGiven {
    float position[3] = {};
    float color[3] = {};       // in linear light, at its middle; past 1 it blooms
    float size = 1.0f;         // its radius
    float seen_through = 0.0f; // how far in front of its middle what stands there still doesn't hide it: its lamp's glass, its flame's body
};

/** Whether flames' and lamps' halos are glows (AddGlow), with the light they threw given as point lights, in place of the game's large halo sprites
 * (the owner's decision of 2026-11-16): with modern light and bloom, unless DC_NOKTUS_HALOS=1 asks for the game's drawn halos, to compare. */
bool ModernGlow();

/** Adds a glow to this tick in the world's camera, hidden by what stands in front of it. False where it can't be (no glows, the world's camera
 * not yet known this tick, or the scene's light already ended this tick: EndLinearLight), for the caller to draw the game's own. True means given,
 * not drawn: Noktus still leaves out a glow past its 1024 a tick, one given while drawing into a surface, or one after the interface has split
 * from the scene. */
bool AddGlow(const GlowGiven &glow);

/** Says that this tick's main light (light 0) is the sun's or the moon's, casting real shadows out to this eye distance (step 8): the world's solid
 * draws cast, and the game's own shadows give way (SunShadows). Nothing without modern light or with DC_NOKTUS_SHADOWS=0. */
void SetSunShadows(float distance);

/** Whether real shadows from the sun stand in for the game's own: with modern light, where a sun was given this tick or the one before (a town's
 * sun or moon, a dungeon's or an interior's main light), unless DC_NOKTUS_SHADOWS=0. Past the sun's shadow distance there are then none: the game's far shadows go too. */
bool SunShadows();

/** The way the sun's light falls for its shadows (never lower than they're cast from) and how far from the eye they reach, as this tick gave them,
 * or the tick before while this one's isn't known yet: false with no sun (SunShadows). */
bool SunFalling(float falling[3], float *distance);

/** Whether point lights reach the meshes drawn from here on: off around surfaces whose light is painted in, as an interior's map is, so they aren't
 * lit twice (step 7's decision 1). On unless switched off. */
void SetPointLit(bool on);

/** Whether the self-lit map drawn from here on (its frames named 'C', showing their painted colour whatever the light) is lit by the scene's main
 * light instead, with modern light (the owner's decision of 2026-11-16): a dungeon's map, so real shadows fall on it. Off unless switched on. */
void SetPaintedLit(bool on);

/** Whether self-lit map frames drawn now are lit (SetPaintedLit), with modern light. */
bool PaintedLit();

/** A soft darkening on the ground under something standing there: see AddContactShadow. */
struct ContactShadowGiven {
    std::uint64_t key = 0;          // who this is from tick to tick, so it moves between ticks: 0 for none
    float         position[3] = {}; // where it stands on the ground
    float         radius = 1.0f;    // how far round it reaches along the ground
    float         strength = 0.5f;  // the share of the light it takes right under it
};

/** A character's contact shadow at its scale 1, Toan standing about 14 units tall: its reach along the ground, and its strength right under it. */
inline constexpr float kCharacterContactRadius = 9.0f;
inline constexpr float kCharacterContactStrength = 0.7f;

/** Adds a contact shadow under a character to this tick's scene (the owner's decision of 2026-11-16), with modern light; nothing otherwise. The
 * world's up is y. */
void AddContactShadow(const ContactShadowGiven &shadow);

/** Lays depth of field and heat haze on the screen as drawn so far, where the game drew its own. */
void ApplyFocus(const FocusSettings &settings);

/** Air rising over a fire, which bends what's seen through it: see ApplyHeat. */
struct HeatGiven {
    float position[3] = {}; // in the world: where the heat rises from
    float radius = 1.0f;    // how far the rising air reaches each side
    float height = 1.0f;    // how far up it rises before it's still
    float strength = 1.0f;  // 1: a torch's
};

/** Whether water is real water where the renderer draws it (Noktus's water_clarity above 0): the rivers and lakes too (RiverWaterScope). */
bool ModernWater();

/** A fire's flame, drawn by the renderer (AddFire): Noktus's Fire. */
struct FireGiven {
    std::uint64_t key = 0;     // who it is from tick to tick, so its flicker is its own
    float position[3] = {};    // in the world: its flame's foot
    float height = 1.0f;       // how tall its flame stands
    float width = 0.5f;        // how wide it is at its widest
    float light[3] = {};       // in linear light, what its heart gives
    float fog = 1.0f;          // how much of it the fog leaves, 0 to 1
    float seen_through = 0.0f; // how far in front of its foot what stands there still doesn't hide it
    float glow = 0.0f;         // how much of its light it gives off round about, from its own shape, for bloom to spread (noktus::Fire::glow)
};

/** Whether fires' flames are the renderer's own (AddFire), in place of the game's sprites (step 11, the owner's decision of 2026-11-18): with modern
 * glows (ModernGlow), unless DC_NOKTUS_FIRE_GAME=1 asks for the game's, to compare. */
bool ModernFire();

/** Gives a fire's flame to the renderer for this tick, if it draws them (ModernFire) and the scene's light hasn't ended: false for the caller to
 * draw the game's. */
bool AddFire(const FireGiven &fire);

/** Whether fires' heat is the renderer's own (ApplyHeat), in place of the game's shimmer (a copy of the screen laid back with its lines shifted):
 * whenever Noktus draws, unless DC_NOKTUS_HEAT=0 asks for the game's. */
bool ModernHeat();

/** Bends what's been drawn so far behind each fire's rising air, where the game drew its own shimmer, in the world's camera. */
void ApplyHeat(std::span<const HeatGiven> heat);

/** Ends the frame's linear light here: what's drawn after it is blended on the finished picture, as the game's effects were tuned to. Nothing without
 * the linear scene. */
void EndLinearLight();

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
