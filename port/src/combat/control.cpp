#include "combat/control.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>

namespace dc::combat {

namespace {

struct Slot {
    std::optional<Reaction> reaction;
    std::size_t             step = 0;
    int                     ticks = 0; // ticks the step has played
};

std::array<Slot, kMonsterSlots> &Slots() {
    static std::array<Slot, kMonsterSlots> slots;
    return slots;
}

bool InRange(int slot) {
    return slot >= 0 && slot < kMonsterSlots;
}

const MotionKey *Find(std::span<const MotionKey> keys, std::string_view name) {
    const auto found = std::ranges::find(keys, name, &MotionKey::name);
    return found == keys.end() ? nullptr : &*found;
}

bool SameFrames(const MotionKey &a, const MotionKey &b) {
    return a.start == b.start && a.end == b.end;
}

// The model has name with frames of its own: some, and not a placeholder on its idle's (the first key's).
const MotionKey *Played(std::span<const MotionKey> keys, std::string_view name) {
    const MotionKey *key = Find(keys, name);
    if (key == nullptr || key->end <= key->start) {
        return nullptr;
    }
    if (key != keys.data() && SameFrames(*key, keys.front())) {
        return nullptr;
    }
    return key;
}

// The first of candidates the model plays, or empty.
std::string First(std::span<const MotionKey> keys, std::initializer_list<std::string_view> candidates) {
    for (std::string_view candidate : candidates) {
        if (Played(keys, candidate) != nullptr) {
            return std::string(candidate);
        }
    }
    return {};
}

// A down lasts two seconds: at their own speed a Skeleton Soldier's fall and get-up took nearly four,
// which felt long in play. Only the time on the ground gives way, so the roll is sped up, never by more
// than this, and never slowed.
constexpr int   kDownTicks = 120;
constexpr float kFastestRoll = 3.0f;

// Where a model's down reaches the ground and where it lies still, in frames from damage_2's first, and
// where it is back on its feet, in frames from get_up's first. Read off the models' animations by the
// height of their heads; a model without them plays its fall and get-up whole.
struct DownMarks {
    int ground;
    int still;
    int upright;
};

constexpr DownMarks kSkeletonMarks{.ground = 4, .still = 20, .upright = 9};
constexpr DownMarks kMageMarks{.ground = 8, .still = 13, .upright = 10};
constexpr DownMarks kE52Marks{.ground = 5, .still = 7, .upright = 9};

constexpr std::array<std::pair<std::string_view, DownMarks>, 18> kDownMarks{
    {
     {"e01a", kSkeletonMarks},
     {"e03a", kSkeletonMarks},
     {"e50a", kSkeletonMarks},
     {"e50a_", kSkeletonMarks},
     {"e129a", kSkeletonMarks},
     {"e131a", kSkeletonMarks},
     {"e44a", kMageMarks},
     {"e45a", kMageMarks},
     {"e46a", kMageMarks},
     {"e47a", kMageMarks},
     {"e48a", kMageMarks},
     {"e132a", kMageMarks},
     {"e138a", kMageMarks},
     {"e144a", kMageMarks},
     {"e150a", kMageMarks},
     {"e161a", kMageMarks},
     {"e52a", kE52Marks},
     {"e52a_", kE52Marks},
     }
};

const DownMarks *Marks(std::string_view model) {
    const auto found = std::ranges::find(kDownMarks, model, &std::pair<std::string_view, DownMarks>::first);
    return found == kDownMarks.end() ? nullptr : &found->second;
}

// After a stagger the monster stands for a moment before its script attacks again.
constexpr int kStaggerStand = 50;

// Master Utan pants this long on his knee.
constexpr int kUtanPant = 120;

} // namespace

HoldTick Hold(int slot, Reaction reaction) {
    if (!InRange(slot)) {
        return {.action = HoldAction::Release};
    }
    Slot &held = Slots()[static_cast<std::size_t>(slot)];
    held = Slot{.reaction = std::move(reaction)};
    if (held.reaction->steps.empty()) {
        held.reaction.reset();
        return {.action = HoldAction::Release};
    }
    return {.action = HoldAction::Play, .step = 0};
}

bool Held(int slot) {
    return InRange(slot) && Slots()[static_cast<std::size_t>(slot)].reaction.has_value();
}

const Reaction *HeldReaction(int slot) {
    return Held(slot) ? &*Slots()[static_cast<std::size_t>(slot)].reaction : nullptr;
}

HoldTick StepHold(int slot, float frame, bool motion_ended) {
    if (!Held(slot)) {
        return {};
    }
    Slot             &held = Slots()[static_cast<std::size_t>(slot)];
    const MotionStep &step = held.reaction->steps[held.step];
    held.ticks++;
    const bool reached = step.until >= 0 ? frame >= static_cast<float>(step.until) : motion_ended;
    const bool done = step.loop_ticks > 0 ? held.ticks >= step.loop_ticks : (reached && held.ticks > 1);
    if (!done) {
        return {};
    }
    held.ticks = 0;
    if (++held.step < held.reaction->steps.size()) {
        return {.action = HoldAction::Play, .step = held.step};
    }
    held.reaction.reset();
    return {.action = HoldAction::Release};
}

void Release(int slot) {
    if (InRange(slot)) {
        Slots()[static_cast<std::size_t>(slot)] = Slot{};
    }
}

void ReleaseAll() {
    Slots().fill(Slot{});
}

std::optional<Reaction> DownReaction(std::string_view model, std::span<const MotionKey> keys) {
    const MotionKey *fall = Played(keys, "damage_2");
    const MotionKey *rise = Played(keys, "get_up");
    if (fall == nullptr || rise == nullptr || SameFrames(*fall, *rise)) {
        return std::nullopt;
    }
    const DownMarks *marks = Marks(model);
    const bool       fits = marks != nullptr && fall->speed > 0 && rise->speed > 0 && marks->ground < marks->still && marks->still < fall->end - fall->start && marks->upright < rise->end - rise->start;
    if (!fits) {
        return Reaction{
            .name = "down", .steps = {{.motion = "damage_2"}, {.motion = "get_up"}}
        };
    }
    const float on_feet = (static_cast<float>(marks->ground) / fall->speed) + (static_cast<float>(marks->upright) / rise->speed);
    const float left = static_cast<float>(kDownTicks) - on_feet;
    const float rolled = static_cast<float>(marks->still - marks->ground);
    const float roll = std::clamp(left > 0 ? rolled / left : fall->speed * kFastestRoll, fall->speed, fall->speed * kFastestRoll);
    return Reaction{
        .name = "down",
        .steps = {{.motion = "damage_2", .until = marks->ground},
                  {.motion = "damage_2", .speed = roll, .until = marks->still, .carry_on = true},
                  {.motion = "get_up", .until = marks->upright}}
    };
}

std::optional<Reaction> StaggerReaction(std::string_view model, std::span<const MotionKey> keys) {
    if (model == "c14a" && Played(keys, "damage_left_foot_enter") && Played(keys, "panting_loop") && Played(keys, "damage_left_foot_return")) {
        return Reaction{
            .name = "stagger",
            .steps = {{.motion = "damage_left_foot_enter"},
                      {.motion = "panting_loop", .loop_ticks = kUtanPant},
                      {.motion = "damage_left_foot_return"}},
            .lock_on = "face"
        };
    }
    const std::string hit = First(keys, {"damage_1", "damage", "damage_6_1", "damage_face", "damage_eye"});
    if (hit.empty()) {
        return std::nullopt;
    }
    Reaction          reaction{.name = "stagger", .steps = {{.motion = hit}}};
    const std::string stand = First(keys, {"idle", "idle_0", "wait", "base_idle_0"});
    if (!stand.empty()) {
        reaction.steps.push_back({.motion = stand, .loop_ticks = kStaggerStand});
    }
    return reaction;
}

std::string_view TestMode() {
    static const char *mode = std::getenv("DC_COMBAT_TEST");
    return mode != nullptr ? std::string_view(mode) : std::string_view();
}

} // namespace dc::combat
