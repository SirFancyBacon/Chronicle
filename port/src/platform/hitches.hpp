#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "platform/load_work_profile.hpp"

namespace dc::platform {

/** A tick's time on the game's thread, in seconds. */
struct TickWork {
    double work = 0.0;    /**< From the thread waking for the tick to handing it to the renderer: the game's logic and its drawing described. */
    double handing = 0.0; /**< Handing it over, up to waiting for the next tick. */
    /** Of the whole, each kind of loading work load_profile times. Kinds can hold one another (a model's build its textures), so they don't add up. */
    std::array<double, static_cast<unsigned>(load_profile::Kind::Count)> loads{};
};

/**
 * Ticks whose work took the game's thread longer than a tick, which a display frame then waits on: the game stutters unless the renderer's cushion hides
 * it. Handing a tick to the renderer isn't counted, since a renderer holding it up shows in its own pacing report. A run of them in a row is told as one,
 * when it ends.
 */
class HitchLog {
public:
    explicit HitchLog(double tick_seconds) : tick_seconds_(tick_seconds) {}

    void SetTickLength(double tick_seconds) { tick_seconds_ = tick_seconds; }

    /** Notes a tick, numbered by the game's frame count. Says how a run went once a tick in time ends it. */
    std::optional<std::string> Note(const TickWork &tick, std::int64_t frame);

    /** Says how a run still going went, as the game ends. */
    std::optional<std::string> Flush();

private:
    double       tick_seconds_;
    std::int64_t first_ = 0;
    std::int64_t last_ = 0;
    int          ticks_ = 0;
    double       longest_ = 0.0;
    TickWork     sum_;
};

/** The game's thread's own tick work ends: MGEndFrame begins. */
void HitchTickWorkEnds();

/** And the tick is handed to the renderer: MGEndFrame waits for the next tick. */
void HitchHandedOver();

/** The game's thread wakes for the next tick: MGEndFrame ends. A run of long ticks is written to stderr as it ends. */
void HitchTickWorkBegins();

/** Writes a run still going, as the game ends. */
void HitchFlush();

} // namespace dc::platform
