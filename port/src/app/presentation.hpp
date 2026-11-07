#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "gfx/gfx.hpp"
#include "platform/config.hpp"

namespace dc::app {

// Noktus shows the game and moves it between ticks: what's left here is the FPS counter and reading pictures back.
// With DC_RENDERER=kazarov, each of these goes to app/kazarov_presentation.hpp instead.

struct GamePresentSettings {
    bool            interpolation = true;              /**< Kazarov's renderer: off presents each tick's canonical image once. */
    double          max_fps = 0.0;                     /**< Kazarov's renderer: display frame rate cap; 0 leaves it to the present mode. */
    int             display_per_tick = 0;              /**< Kazarov's renderer: headless display renders per tick, at alphas k / n. */
    bool            show_fps = false;                  /**< Initial FPS counter state. */
    ConfigFpsDetail fps_detail = ConfigFpsDetail::All; /**< What the FPS counter shows. */
};

void GameSetPresentSettings(const GamePresentSettings &settings);

/** A tick's drawing is finished. Noktus has no use for the list, which is null when it's drawing alone. */
void GameTickDone(gfx::DisplayListRef list);

/** While the game waits for the next tick. False, as nothing here has frames to show. */
bool GamePresentBetweenTicks(double fraction, std::chrono::steady_clock::time_point next_tick);

/** The end of a tick. */
void GamePresentTickEnd();

/** Whether the FPS counter is drawn now. */
bool GameShowingFps();

/** What the FPS counter says: frames shown a second, the measured and configured tick rates and the newest tick's draws. */
std::string GameFpsText();

/** Draws the FPS counter into the tick. */
void GameDrawFps();

/** The last tick as it ended, RGBA8 rows top to bottom: what --screenshot writes. */
bool GameScreenshot(std::vector<std::uint8_t> &rgba, std::uint32_t &width, std::uint32_t &height);

} // namespace dc::app
