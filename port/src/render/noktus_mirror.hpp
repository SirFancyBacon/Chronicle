#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "gfx/gfx.hpp"

struct SDL_Window;

namespace dc::render {

// DC_RENDERER=kazarov: Kazarov's renderer draws the game, for telling which renderer a bug is in. Noktus draws by default, and Kazarov's renderer doesn't start.
// DC_NOKTUS_MIRROR=<path>: writes the last picture to path at exit. With DC_RENDERER=kazarov, Noktus also draws everything alongside, with no window, to compare.
// DC_NOKTUS_MOMENT=<0 to 1>: also writes the last tick as drawn that far from the tick before, beside it as <name>.between.png.
// DC_NOKTUS_PAINT_WINDOW=1 paints the window magenta in place of the picture, and DC_NOKTUS_TARGET_TIMES=0 paces frames by watching them arrive.
// DC_NOKTUS_CUSHION=<ticks> sets how far behind the newest tick it stands, and DC_NOKTUS_INTERPOLATE=0 shows each tick as it ended. How the frames went is printed at exit.
// DC_NOKTUS_DEPTH_PROBE=1 asks for the depth at the middle of the frame every tick, to exercise depth reads whatever the scene does. Beside Kazarov's renderer, how the two compared is printed at exit.
// DC_NOKTUS_WORKERS=<n> sets the threads sharing the work, and DC_NOKTUS_PARALLEL_FROM=<draws> how many mesh draws a tick needs before they're shared.

/** What the Options screen says about drawing. */
struct MirrorSettings {
    bool             interpolate = true;
    gfx::PresentMode present_mode = gfx::PresentMode::Fifo;
    double           max_fps = 0.0;
    int              anisotropy = 0;
};

/** What DC_RENDERER's value asks for: only "kazarov" means Kazarov's renderer. */
bool KazarovAsked(const char *setting);

/** Whether DC_RENDERER asked for Kazarov's renderer to draw. It's a test tool, so it has no setting: older builds saved video.renderer as kazarov. */
bool KazarovDraws();

/** Whether Noktus can be drawn to: not before it has its window, or after it stops. */
bool MirrorOn();

/** Noktus draws to this window, on its own thread and paced to the display, or into a picture with no window at all. Nothing is drawn before this. With Kazarov's renderer drawing, the window is its own, and Noktus only starts if DC_NOKTUS_MIRROR asks. */
void MirrorTakeWindow(SDL_Window *window); // null for a run with no window

/** Lets go of a window about to be closed, for the first-run screen, which has one of its own. */
void MirrorDropWindow();

/** The GPU drawing the game. */
std::string MirrorDeviceName();

/** The last tick as it ended, as RGBA. False before there is one. */
bool MirrorReadScreen(std::vector<std::uint8_t> &pixels, std::uint32_t &width, std::uint32_t &height);

/** Whether it has the game's window. */
bool MirrorHasWindow();

/** Takes the settings as they stand, now and for when the mirror starts. */
void MirrorApply(const MirrorSettings &settings);

/** The ticks a second to expect, as the setting changes. */
void MirrorTickRate(double ticks_per_second);

/** Mesh and 2D draws the last tick made. */
std::uint64_t MirrorDrawsLastTick();

/** Asks how far away the scene is under part of the frame, as the tick stands now. Answers come some ticks later: see MirrorDepthResult. */
void MirrorReadDepth(std::uint32_t slot, const gfx::LogicalRect &area);
std::optional<float> MirrorDepthResult(std::uint32_t slot);
/** Beside Kazarov's renderer: looks for its answer among Noktus's recent ones, since the two come a tick or so apart. How they went is printed at exit. */
void MirrorCompareDepth(std::uint32_t slot, float today);
/** Whether DC_NOKTUS_DEPTH_PROBE asked for a read every tick. */
bool MirrorDepthProbe();

/** Frames shown a second, over the last few ticks. */
double MirrorFramesPerSecond();

/** The moment asked for by DC_NOKTUS_MOMENT, if any. */
std::optional<float> MirrorMoment();

void MirrorBeginTick();
void MirrorEndTick();

/** Finishes the mirror while its window can still be used. */
void MirrorStop();

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
/** A palette's colours from first on, for the paletted textures drawn through it. */
void MirrorPalette(gfx::TextureHandle palette, const std::uint32_t *colors, std::uint32_t first, std::uint32_t count);

bool MirrorCopyDepth(const gfx::LogicalRect &source);
void MirrorBlit(gfx::TextureHandle from, gfx::Rect from_rect, gfx::TextureHandle to, gfx::Rect to_rect, gfx::Filter filter);

void MirrorCreateMesh(gfx::MeshHandle mesh, std::span<const gfx::Vertex3D> vertices, std::span<const std::uint32_t> indices);
void MirrorUpdateMesh(gfx::MeshHandle mesh, std::uint32_t first, std::span<const gfx::Vertex3D> vertices);
void MirrorDestroyMesh(gfx::MeshHandle mesh);
void MirrorDrawMesh(gfx::MeshHandle mesh, std::uint32_t first_index, std::uint32_t index_count, const gfx::MeshConstants &constants,
                    const gfx::TextureBinding &texture, const gfx::DrawState &state, const gfx::MeshTransform *transform);
void MirrorDrawImmediate(std::span<const gfx::Vertex3D> vertices, std::span<const std::uint32_t> indices, const gfx::MeshConstants &constants,
                         const gfx::TextureBinding &texture, const gfx::DrawState &state, const gfx::MeshTransform *transform);

void MirrorDraw2D(gfx::Primitive primitive, std::span<const gfx::Vertex2D> vertices, const gfx::TextureBinding &texture,
                  const gfx::DrawState &state, bool over_the_window = false);

} // namespace dc::render
