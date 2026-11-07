#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "platform/config.hpp"

namespace dc::app {

// Noktus shows the game and moves it between ticks: what's left here is the FPS counter and reading pictures back.

struct GamePresentSettings {
    bool            show_fps = false;                  /**< Initial FPS counter state. */
    ConfigFpsDetail fps_detail = ConfigFpsDetail::All; /**< What the FPS counter shows. */
};

void GameSetPresentSettings(const GamePresentSettings &settings);

/** A tick's drawing is finished. */
void GameTickDone();

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
