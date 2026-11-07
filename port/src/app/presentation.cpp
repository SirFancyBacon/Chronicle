#include "app/presentation.hpp"

#include <chrono>
#include <format>

#include "platform/clock.hpp"
#include "platform/input.hpp"
#include "platform/overlay.hpp"
#include "render/noktus_mirror.hpp"
#include "render/passes.hpp"
#include "text/textwipe.hpp"

namespace dc::app {
namespace {

using PresentClock = std::chrono::steady_clock;

struct FpsCounter {
    bool             on = false;
    ConfigFpsDetail  detail = ConfigFpsDetail::All;
    OverlayRate      ticks;
};

FpsCounter g_fps;
bool       g_polled = false; // this tick

void PollFpsToggle() {
    if (g_polled) {
        return;
    }
    g_polled = true;
    dc::text::textwipe::Update();
    if (InputHostPressed(InputHostAction::FpsToggle)) {
        g_fps.on = !g_fps.on;
    }
}

} // namespace

void GameSetPresentSettings(const GamePresentSettings &settings) {
    g_fps.on = settings.show_fps;
    g_fps.detail = settings.fps_detail;
}

void GameTickDone() {
    render::PassTraceTick(static_cast<std::uint32_t>(render::MirrorDrawsLastTick()), true);
    g_fps.ticks.Count(PresentClock::now());
}

bool GamePresentBetweenTicks(double /*fraction*/, std::chrono::steady_clock::time_point /*next_tick*/) {
    PollFpsToggle();
    return false;
}

void GamePresentTickEnd() {
    PollFpsToggle();
    g_polled = false;
}

bool GameShowingFps() {
    return g_fps.on;
}

std::string GameFpsText() {
    // With no window, frames are ticks.
    std::string text = std::format("FPS {:.1f}", render::MirrorHasWindow() ? render::MirrorFramesPerSecond() : g_fps.ticks.PerSecond());
    if (g_fps.detail != ConfigFpsDetail::Fps) {
        text += std::format("  TICK {:.1f}/{:g}", g_fps.ticks.PerSecond(), ClockTickRate());
    }
    if (g_fps.detail == ConfigFpsDetail::All) {
        text += std::format("  DRAWS {}", render::MirrorDrawsLastTick());
    }
    return text;
}

void GameDrawFps() {
    if (!g_fps.on) {
        return;
    }
    render::PassScope    pass(render::Pass::Overlay);
    render::Layer2DScope layer(render::Layer2D::Ui, render::Layer2DScope::kDiffersFromRule); // text in the window's corner, whatever its shape
    OverlayRecord(GameFpsText());
}

bool GameScreenshot(std::vector<std::uint8_t> &rgba, std::uint32_t &width, std::uint32_t &height) {
    return render::MirrorReadScreen(rgba, width, height);
}

} // namespace dc::app
