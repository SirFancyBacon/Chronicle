#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "app/presentation.hpp"
#include "gfx/gfx.hpp"

namespace dc::app::kazarov {

// Each tick (MGBeginFrame to MGEndFrame) is recorded as a display list. MGEndFrame hands it to
// GameRenderTick, which renders it once as the canonical image the next tick's effects sample;
// then, while MGEndFrame waits for the next tick, GamePresentBetweenTicks presents display frames
// interpolated between the last two lists at the elapsed fraction of the tick, and
// GamePresentTickEnd presents the canonical image if nothing was presented during the tick.
// app/presentation.cpp calls these with DC_RENDERER=kazarov.

void GameSetPresentSettings(const GamePresentSettings &settings);

/** Renders a finished tick's display list once as the canonical image and keeps it, with the previous one, for the display frames presented until the next tick. */
void GameRenderTick(gfx::DisplayListRef list);

/** Presents a display frame interpolated at fraction of the tick, pacing to max_fps and never sleeping past next_tick. False once nothing more should be presented this tick. */
bool GamePresentBetweenTicks(double fraction, std::chrono::steady_clock::time_point next_tick);

/** Presents the canonical image if no display frame was presented during the tick. */
void GamePresentTickEnd();

/** Stops the render thread presenting before readback or renderer shutdown. The newest lists stay, for GameScreenshotBetween. */
void GameStopPresentation();

/** Counters of rendering and presentation since start-up. */
struct GamePresentStats {
    std::uint64_t ticks;             /**< Canonical renders. */
    std::uint64_t display_frames;    /**< Display renders, game and render thread together. */
    double        canonical_seconds; /**< Time spent in canonical renders. */
    double        display_seconds;   /**< Time spent in display renders. */
    std::uint64_t mesh_draws;        /**< Mesh draws over all ticks. */
    std::uint64_t keyed_mesh_draws;  /**< Mesh draws with an interpolation key. */
    std::uint64_t draws_2d;          /**< 2D draws over all ticks. */
    std::uint64_t stateful;          /**< State commands over all ticks. */
    std::uint64_t max_draws;         /**< Most mesh and 2D draws in one tick. */
    std::uint64_t last_draws;        /**< Mesh and 2D draws of the newest tick. */
};

/** The presentation counters so far, including the render thread's display frames. */
GamePresentStats GamePresentStatistics();

/** Turns the --perf-log report on or off: once a second, the game thread's tick times and the renderer's gfx::FrameTimings on stderr. */
void GameSetPerfLog(bool on);

bool        GameShowingFps();
std::string GameFpsText();

/** The newest tick's image with the FPS counter over it, as a window shows it, read back. False where the counter is off or no display render can be made. */
bool GameScreenshotWithFps(std::vector<std::uint8_t> &rgba, std::uint32_t &width, std::uint32_t &height);

/** The last tick as a display frame shows it that far from the tick before, for comparing renderers. */
bool GameScreenshotBetween(float moment, std::vector<std::uint8_t> &rgba, std::uint32_t &width, std::uint32_t &height);

/** The newest canonical image (or the loading screen's frame, if it presented since), RGBA8 rows top to bottom. A display frame, and so the FPS counter, never is. Outside a frame only. */
bool GameScreenshot(std::vector<std::uint8_t> &rgba, std::uint32_t &width, std::uint32_t &height);

} // namespace dc::app::kazarov
