#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "gfx/gfx.hpp"

struct SDL_Window;

namespace dc::render {

enum class Pass : std::uint8_t;
enum class Step : std::uint8_t;

// DC_RENDERER=kazarov: Kazarov's renderer draws the game, for telling which renderer a bug is in. Noktus draws by default, and Kazarov's renderer doesn't start.
// DC_NOKTUS_MIRROR=<path>: writes the last picture to path at exit. With DC_RENDERER=kazarov, Noktus also draws everything alongside, with no window, to compare.
// DC_NOKTUS_HDR=hdr10, scrgb or off overrides the HDR setting for a run, for scripted tests. DC_NOKTUS_HDR_FORCE=1 says the display is in HDR when the system says it isn't.
// DC_NOKTUS_MOMENT=<0 to 1>: also writes the last tick as drawn that far from the tick before, beside it as <name>.between.png.
// DC_NOKTUS_PAINT_WINDOW=1 paints the window magenta in place of the picture, and DC_NOKTUS_TARGET_TIMES=0 paces frames by watching them arrive.
// DC_NOKTUS_CUSHION=<ticks> sets how far behind the newest tick it stands, and DC_NOKTUS_INTERPOLATE=0 shows each tick as it ended. How the frames went is printed at exit.
// DC_NOKTUS_DEPTH_PROBE=1 asks for the depth at the middle of the frame every tick, to exercise depth reads whatever the scene does. Beside Kazarov's renderer, how the two compared is printed at exit.
// DC_NOKTUS_CLASS_VIEW=1 draws each draw in its class's colour (noktus::DrawClass), for checking classes by eye.
// DC_NOKTUS_MOTION=1 writes motion and reactive beside the scene (noktus::ReadMotion). With DC_NOKTUS_MOTION_OUT=<prefix> too, at exit the last tick drawn halfway
// and at its end (<prefix>.first.png, .second.png) and the second's motion and reactive (.motion.f32, .reactive.f32, .motion.json), for the harness's motion check.
// Unpaced (offscreen) runs only: paced, frames drawn to be read don't count as frames shown, so the second's motion isn't from the first.
// DC_NOKTUS_MOTION_VIEW=1 shows the motion in place of the picture, screenshots included: direction as hue, length as brightness, reactive in magenta.
// DC_NOKTUS_ANTI_ALIASING=off, smaa, smaa_t2x or taa overrides the anti-aliasing setting for a run, for scripted tests.
// DC_NOKTUS_WORKERS=<n> sets the threads sharing the work, and DC_NOKTUS_PARALLEL_FROM=<draws> how many mesh draws a tick needs before they're shared.

/** What's sent to a display in HDR. */
enum class MirrorHdr : std::uint8_t {
    Off,
    Hdr10,
    ScRgb,
};

/** How edges are smoothed, at the picture's own size. */
enum class MirrorAntiAliasing : std::uint8_t {
    Off,
    Smaa,
    SmaaT2x,
    Taa,
};

/** What the Options screen says about drawing. */
struct MirrorSettings {
    bool             interpolate = true;
    gfx::PresentMode present_mode = gfx::PresentMode::Fifo;
    double           max_fps = 0.0;
    int              anisotropy = 0;
    MirrorAntiAliasing anti_aliasing = MirrorAntiAliasing::Off;
    MirrorHdr        hdr = MirrorHdr::Off;
    float            paper_white = 0.0f; // nits; 0 takes what the system says
    float            peak = 0.0f;        // nits; 0 takes what the system says
    bool             force_hdr = false;  // DC_NOKTUS_HDR_FORCE: the display is in HDR, whatever the system says
};

/** What goes to the window, once the settings have met what the system says. */
struct HdrOutput {
    MirrorHdr mode = MirrorHdr::Off;
    float     paper_white = 203.0f;
    float     peak = 1000.0f;
    bool      operator==(const HdrOutput &) const = default;
};

/** A rectangle of the frame shown at nits of its own in HDR, past the peak, for setting HDR by eye. */
struct MirrorHdrPatch {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float nits = 0.0f;
};

/**
 * Works out HDR's output. The system's white is in scRGB's terms, where 1 is 80 nits, and its headroom is how far past that white the display goes.
 * A display not in HDR stays SDR whatever's saved, as it would be sent far too bright a picture, unless force_hdr says it's known to be in HDR, for when the system can't tell.
 */
HdrOutput ChooseHdr(const MirrorSettings &settings, bool display_hdr, float white, float headroom);

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

/** Names the pass Noktus records drawing in from now on. */
void MirrorSetPass(Pass pass);

/** Steps reach Noktus as they're begun and ended. */
void MirrorBeginStep(Step step);
void MirrorEndStep();

/** A step of the tick Noktus last ended, and what it found it reads and writes. */
struct MirrorStepReport {
    Step          step{};
    std::uint32_t commands = 0;
    bool          reads_screen = false;
    bool          reads_screen_depth = false;
    bool          writes_screen = false;
    bool          writes_screen_depth = false;
    std::uint32_t textures_read = 0;
    std::uint32_t textures_written = 0;
};

std::vector<MirrorStepReport> MirrorTickSteps();

/** A run of the last tick's draws and blits in one pass, as Noktus recorded them. */
struct MirrorPassStretch {
    Pass          pass;
    std::uint32_t draws = 0;
    std::uint32_t copies = 0;
};

/** The passes of the last tick Noktus was given, in order. Empty when Noktus isn't drawing. */
std::vector<MirrorPassStretch> MirrorTickPasses();

/** How many draws Noktus put in each class since the last call, by pass and by the rule that classed them. */
struct MirrorClassTally {
    Pass          pass;
    const char   *draw_class = ""; /**< Lives as long as the program, as rule does. */
    const char   *rule = "";
    std::uint64_t draws = 0;
};
std::vector<MirrorClassTally> MirrorTakeClassTally();

/** What the window is being sent: Off without HDR, a window or Noktus drawing it, or when HDR asked for fell back to SDR. */
HdrOutput MirrorHdrShown();

/** Patches drawn over the picture in HDR until others are given: up to two, later over earlier. Empty takes them away. */
void MirrorHdrPatches(std::span<const MirrorHdrPatch> patches);

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
