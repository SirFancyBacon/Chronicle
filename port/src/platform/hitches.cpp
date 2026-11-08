#include "platform/hitches.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>

#include "app/gameloop.hpp"
#include "platform/clock.hpp"

namespace dc::platform {

namespace {

constexpr std::array<const char *, static_cast<unsigned>(load_profile::Kind::Count)> kLoadNames = {
    "background reads", "metadata", "PNGs", "textures", "model builds", "meshes", "reads", "file reads", "sound", "glyphs", "GPU objects",
};

double Ms(double seconds) {
    return seconds * 1000.0;
}

} // namespace

std::optional<std::string> HitchLog::Note(const TickWork &tick, std::int64_t frame) {
    // Only the game's own work: a renderer that holds the hand-over up shows in Noktus's pacing report, as frames held.
    if (tick.work > tick_seconds_) {
        first_ = ticks_ == 0 ? frame : first_;
        last_ = frame;
        ticks_++;
        longest_ = std::max(longest_, tick.work);
        sum_.work += tick.work;
        sum_.handing += tick.handing;
        for (std::size_t i = 0; i < sum_.loads.size(); i++) {
            sum_.loads[i] += tick.loads[i];
        }
        sum_.files += tick.files;
        return std::nullopt;
    }
    return Flush();
}

std::optional<std::string> HitchLog::Flush() {
    if (ticks_ == 0) {
        return std::nullopt;
    }
    std::string line = ticks_ == 1 ? std::format("hitch: frame {}, a tick of {:.1f} ms took the game {:.1f} ms", first_, Ms(tick_seconds_), Ms(longest_))
                                   : std::format("hitch: frames {} to {}, {} ticks of {:.1f} ms in a row took the game {:.1f} ms, the longest {:.1f} ms", first_, last_, ticks_,
                                                 Ms(tick_seconds_), Ms(sum_.work), Ms(longest_));
    line += std::format(" (and handing {} over {:.1f})", ticks_ == 1 ? "it" : "them", Ms(sum_.handing));
    std::string loads;
    for (std::size_t i = 0; i < sum_.loads.size(); i++) {
        if (sum_.loads[i] >= 0.00005) {
            loads += std::format("{}{} {:.1f}", loads.empty() ? "; of it " : ", ", kLoadNames[i], Ms(sum_.loads[i]));
            if (static_cast<load_profile::Kind>(i) == load_profile::Kind::Files) {
                loads += std::format(" ({} files)", sum_.files);
            }
        }
    }
    line += loads;
    ticks_ = 0;
    longest_ = 0.0;
    sum_ = {};
    return line;
}

namespace {

using Clock = std::chrono::steady_clock;

struct GameThread {
    HitchLog                           log{1.0 / 60.0};
    bool                               started = false;
    Clock::time_point                  woke;
    Clock::time_point                  work_ended;
    Clock::time_point                  handed;
    decltype(load_profile::counters)   loads_at_wake{};
};

GameThread &State() {
    static GameThread state;
    return state;
}

double Seconds(Clock::duration duration) {
    return std::chrono::duration<double>(duration).count();
}

} // namespace

void HitchTickWorkEnds() {
    State().work_ended = Clock::now();
}

void HitchHandedOver() {
    State().handed = Clock::now();
}

void HitchTickWorkBegins() {
    GameThread &state = State();
    const auto  now = Clock::now();
    if (state.started) {
        TickWork tick;
        tick.work = Seconds(state.work_ended - state.woke);
        tick.handing = Seconds(state.handed - state.work_ended);
        for (unsigned i = 0; i < tick.loads.size(); i++) {
            tick.loads[i] = load_profile::MillisecondsSince(state.loads_at_wake, static_cast<load_profile::Kind>(i)) / 1000.0;
        }
        const auto files = static_cast<unsigned>(load_profile::Kind::Files);
        tick.files = load_profile::counters[files].calls - state.loads_at_wake[files].calls;
        state.log.SetTickLength(1.0 / ClockTickRate());
        if (const std::optional<std::string> line = state.log.Note(tick, app::GameFrameCount())) {
            std::fprintf(stderr, "%s\n", line->c_str());
        }
    }
    // Timing loads costs two clock reads each, so it stays on for the game's thread: a hitch can't be caught after it's happened.
    load_profile::active = true;
    state.started = true;
    state.woke = now;
    state.loads_at_wake = load_profile::counters;
}

void HitchFlush() {
    if (const std::optional<std::string> line = State().log.Flush()) {
        std::fprintf(stderr, "%s\n", line->c_str());
    }
}

} // namespace dc::platform
