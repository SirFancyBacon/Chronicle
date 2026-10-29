#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "fishing/framework.hpp"

namespace dc::fishing {

namespace {

using Json = nlohmann::json;

struct Weighted {
    const Species *species;
    double         weight;
};

std::vector<Weighted> ParseWeights(const Json &list, std::string_view where) {
    if (!list.is_array() || list.empty()) {
        throw std::runtime_error(std::format("{} needs a list of fish", where));
    }
    std::vector<Weighted> weights;
    for (const Json &entry : list) {
        if (!entry.is_object() || !entry.contains("fish") || !entry["fish"].is_string() || !entry.contains("weight") ||
            !entry["weight"].is_number()) {
            throw std::runtime_error(std::format("{}: each fish needs \"fish\" and \"weight\"", where));
        }
        const std::string id = entry["fish"].get<std::string>();
        const Species    *species = FindSpecies(id);
        if (species == nullptr) {
            throw std::runtime_error(std::format("{}: no species is {}", where, id));
        }
        const double weight = entry["weight"].get<double>();
        if (!std::isfinite(weight) || weight <= 0.0) {
            throw std::runtime_error(std::format("{}: {}'s weight must be above 0", where, id));
        }
        weights.push_back({species, weight});
    }
    return weights;
}

const Species *Draw(const std::vector<Weighted> &weights, double roll) {
    double total = 0.0;
    for (const Weighted &entry : weights) {
        total += entry.weight;
    }
    roll *= total;
    for (const Weighted &entry : weights) {
        if (roll < entry.weight) {
            return entry.species;
        }
        roll -= entry.weight;
    }
    return weights.back().species;
}

/**
 * Retail's spawn: each fish drawn by weight from the place's table, or from its rare table one time in
 * the time of day's interval.
 */
class RetailSpawn final : public SpawnStage {
public:
    explicit RetailSpawn(const Json &settings) {
        fish_ = ParseWeights(settings.contains("fish") ? settings["fish"] : Json(), "spawn");
        if (!settings.contains("rare")) {
            return;
        }
        const Json &rare = settings["rare"];
        if (!rare.is_object() || !rare.contains("interval") || !rare["interval"].is_object()) {
            throw std::runtime_error("spawn: rare needs an interval for each time of day");
        }
        Rare parsed{.fish = ParseWeights(rare.contains("fish") ? rare["fish"] : Json(), "spawn rare")};
        constexpr std::array<const char *, 4> kTimes = {"day", "evening", "night", "morning"};
        for (std::size_t time = 0; time < kTimes.size(); time++) {
            const Json &interval = rare["interval"].contains(kTimes[time]) ? rare["interval"][kTimes[time]] : Json();
            if (!interval.is_number_integer() || interval.get<long long>() < 1 || interval.get<long long>() > 1000000) {
                throw std::runtime_error(std::format("spawn: rare interval {} must be a whole number from 1", kTimes[time]));
            }
            parsed.interval[time] = interval.get<int>();
        }
        rare_ = std::move(parsed);
    }

    std::vector<const Species *> Spawn(const Place &place, const SpawnContext &context) const override {
        std::vector<const Species *> spawned;
        for (int fish = 0; fish < place.fish_count; fish++) {
            const bool rare = rare_ && context.random() * rare_->interval[static_cast<std::size_t>(context.time)] < 1.0;
            spawned.push_back(Draw(rare ? rare_->fish : fish_, context.random()));
        }
        return spawned;
    }

private:
    struct Rare {
        std::array<int, 4>    interval{};
        std::vector<Weighted> fish;
    };

    std::vector<Weighted> fish_;
    std::optional<Rare>   rare_;
};

double Chance(const Json &settings, const char *key, std::string_view where) {
    const Json &value = settings.contains(key) ? settings[key] : Json();
    if (!value.is_number() || value.get<double>() < 0.0 || value.get<double>() > 1.0) {
        throw std::runtime_error(std::format("{}: {} must be a chance from 0 to 1", where, key));
    }
    return value.get<double>();
}

int Ticks(const Json &value, std::string_view what) {
    if (!value.is_number_integer() || value.get<long long>() < 1 || value.get<long long>() > 100000) {
        throw std::runtime_error(std::format("{} must be a whole number of ticks from 1", what));
    }
    return value.get<int>();
}

/**
 * Retail's bite (CFish's FindFood, EatFood, BiteHook, LeaveFood and LeaveHook): a fish within the
 * bait's radius notices it at its liking x 0.3 a check, takes it and then the hook at x 0.5, and
 * loses interest at 0.1, every 21 to 50 ticks. The port's own: it holds the hook for hook_ticks.
 */
class RetailBite final : public BiteStage {
public:
    explicit RetailBite(const Json &settings) {
        const Json &hold = settings.contains("hook_ticks") ? settings["hook_ticks"] : Json();
        if (!hold.is_array() || hold.size() != 2) {
            throw std::runtime_error("bite: hook_ticks needs the fewest and the most");
        }
        hold_first_ = Ticks(hold[0], "bite: hook_ticks");
        hold_last_ = Ticks(hold[1], "bite: hook_ticks");
        if (hold_last_ < hold_first_) {
            throw std::runtime_error("bite: hook_ticks must not end before it starts");
        }
        lose_early_ = Chance(settings, "lose_bait_early", "bite");
        lose_escaped_ = Chance(settings, "lose_bait_escaped", "bite");
    }

    ActionStep Update(const FishView &fish, const BiteContext &context) const override {
        // Retail's timer, 20 to 49, which runs a tick past its value.
        int        timer = 20 + static_cast<int>(context.random() * 30.0);
        const bool bait = !context.bait.empty() && context.radius > 0.0f;
        const auto chance = [&](double probability) { return context.random() < probability; };
        const double liking = bait && fish.species != nullptr ? fish.species->Affinity(context.bait, context.default_affinity) : 0.0;
        const bool   at_bait = bait && fish.distance <= 1.0f;

        switch (fish.action) {
            case FishAction::None:
                if (bait && fish.distance <= context.radius && chance(liking * 0.3)) {
                    return {FishAction::Approach, timer + 1};
                }
                return {FishAction::None, timer + 1};
            case FishAction::Approach: {
                FishAction next = FishAction::Approach;
                if (at_bait && chance(liking * 0.5)) {
                    next = FishAction::Eat;
                }
                if (!bait || chance(0.1)) {
                    next = FishAction::Leave;
                    timer >>= 1;
                }
                return {next, timer + 1};
            }
            case FishAction::Eat: {
                FishAction next = FishAction::Eat;
                if (at_bait && chance(liking * 0.5)) {
                    next = FishAction::Bite;
                }
                if (!bait || chance(0.1)) {
                    next = FishAction::Leave;
                }
                if (next == FishAction::Bite) {
                    return {next, hold_first_ + static_cast<int>(context.random() * (hold_last_ - hold_first_ + 1))};
                }
                return {next, timer + 1};
            }
            case FishAction::Bite:
                return {FishAction::Leave, timer * 2 + 1};
            case FishAction::Leave:
                return {FishAction::None, timer + 1};
        }
        return {FishAction::None, timer + 1};
    }

    bool LosesBait(LossCause cause, const BiteContext &context) const override {
        return context.random() < (cause == LossCause::PressedEarly ? lose_early_ : lose_escaped_);
    }

private:
    int    hold_first_ = 0;
    int    hold_last_ = 0;
    double lose_early_ = 0.0;
    double lose_escaped_ = 0.0;
};

/**
 * Retail's fight, which isn't one: the controller rumbles for ticks while the fish's model loads,
 * then it is landed.
 */
class RetailFight final : public FightStage {
public:
    explicit RetailFight(const Json &settings) {
        ticks_ = Ticks(settings.contains("ticks") ? settings["ticks"] : Json(), "fight: ticks");
    }

    FightResult Step(const FightContext &context) const override {
        const bool landed = context.ticks > ticks_ && context.model_ready();
        context.rumble(80 + static_cast<int>(context.random() * 40.0), 10);
        return landed ? FightResult::Landed : FightResult::Fighting;
    }

private:
    int ticks_ = 0;
};

/**
 * Retail's evaluation: the Mardan Garayan count, the points, the records, the message.
 */
class RetailOutcome final : public OutcomeStage {
public:
    void Apply(Catch &fish, const OutcomeContext &context) const override {
        if (fish.species != nullptr && fish.species->mardan_garayan) {
            context.count_mardan_garayan();
        }
        context.add_points(fish.points);
        context.record(fish);
        context.show(fish);
    }
};

// A whole number of ticks from the first to the last of a two-number range.
struct Span {
    int first = 0;
    int last = 0;

    int Draw(const std::function<double()> &random) const { return first + static_cast<int>(random() * (last - first + 1)); }
};

Span ParseSpan(const Json &value, std::string_view what) {
    if (!value.is_array() || value.size() != 2) {
        throw std::runtime_error(std::format("{} needs the fewest ticks and the most", what));
    }
    const Span span{Ticks(value[0], what), Ticks(value[1], what)};
    if (span.last < span.first) {
        throw std::runtime_error(std::format("{} must not end before it starts", what));
    }
    return span;
}

/**
 * Dark Cloud 2's liking levels (F:3815), which weigh a fish's chance to come 0, 0.5, 1 and 1.5; Dark
 * Cloud 1's likings are 0, 0.2, 0.5 and 1.
 */
enum class Level {
    None,
    Low,
    Normal,
    High,
};

Level LevelOf(double liking) {
    if (liking <= 0.0) {
        return Level::None;
    }
    if (liking < 0.35) {
        return Level::Low;
    }
    return liking < 0.75 ? Level::Normal : Level::High;
}

/**
 * Dark Cloud 2's bite (GetUkiWaitTime, F:3783): as the float settles one roll chooses a fish in the
 * water by its liking for the bait, or nothing (then another roll after retry ticks). The chosen fish
 * swims over in at least wait ticks, nibbles for its liking's nibble, then holds the hook for its window;
 * missed, it leaves and the roll starts again. The other fish leave the bait alone.
 */
class BriskBite final : public BiteStage {
public:
    static constexpr int   kLookAgain = 30;        // ticks, while no fish in the water likes the bait
    static constexpr float kRetailSpeed = 0.1f;    // CFish's approach
    static constexpr float kFastestSpeed = 0.6f;   // about retail's quickest swimming

    explicit BriskBite(const Json &settings) {
        const auto at = [&](const char *key) { return settings.contains(key) ? settings[key] : Json(); };
        wait_ = ParseSpan(at("wait"), "bite: wait");
        retry_ = Ticks(at("retry"), "bite: retry");
        const Json nothing = at("nothing");
        if (!nothing.is_number() || nothing.get<double>() < 0.0) {
            throw std::runtime_error("bite: nothing must be a weight from 0");
        }
        nothing_ = nothing.get<double>();
        const Json nibble = at("nibble");
        if (!nibble.is_object()) {
            throw std::runtime_error("bite: nibble needs low, normal and high");
        }
        nibble_low_ = ParseSpan(nibble.contains("low") ? nibble["low"] : Json(), "bite: nibble low");
        nibble_normal_ = ParseSpan(nibble.contains("normal") ? nibble["normal"] : Json(), "bite: nibble normal");
        nibble_high_ = ParseSpan(nibble.contains("high") ? nibble["high"] : Json(), "bite: nibble high");
        window_ = ParseSpan(at("window"), "bite: window");
        lose_early_ = Chance(settings, "lose_bait_early", "bite");
        lose_escaped_ = settings.contains("lose_bait_escaped") ? Chance(settings, "lose_bait_escaped", "bite") : 0.0;
    }

    ActionStep Update(const FishView &fish, const BiteContext &context) const override {
        BiteSession *session = context.session;
        const bool   bait = !context.bait.empty() && context.radius > 0.0f;
        const auto   away = [&](int ticks) {
            return ActionStep{fish.action == FishAction::None || fish.action == FishAction::Leave ? FishAction::None : FishAction::Leave, ticks};
        };
        if (session == nullptr || !bait || !context.waiting) {
            return away(30);
        }
        if (!session->rolled) {
            if (context.now < session->until) {
                return away(1);
            }
            Roll(*session, context);
        }
        if (context.fish_index != session->chosen) {
            return away(1);
        }
        // A fish the roll has just chosen starts its own wait, whatever it was doing, and swims fast
        // enough to reach the bait in it: Dark Cloud 2's fish are on the bait when the wait ends.
        if (session->step == 0 || fish.action == FishAction::None || fish.action == FishAction::Leave) {
            const int wait = wait_.Draw(context.random);
            session->step = 1;
            session->until = context.now + wait * 2;
            session->speed = std::clamp(fish.distance / static_cast<float>(wait), kRetailSpeed, kFastestSpeed);
            return {FishAction::Approach, wait};
        }
        switch (fish.action) {
            case FishAction::Approach:
                // Dark Cloud 1's fish can be seen: it nibbles once it is at the bait. One the water
                // keeps from it for twice its wait swims off, and another roll comes.
                if (fish.distance > 2.0f) {
                    if (context.now < session->until) {
                        return {FishAction::Approach, 10};
                    }
                    *session = {};
                    return {FishAction::Leave, 60};
                }
                return {FishAction::Eat, NibbleFor(fish, context).Draw(context.random)};
            case FishAction::Eat:
                return {FishAction::Bite, window_.Draw(context.random)};
            case FishAction::Bite:
                // Missed: it swims off, and the next roll comes as it goes.
                *session = {};
                return {FishAction::Leave, 60};
            default:
                break;
        }
        return away(1);
    }

    bool LosesBait(LossCause cause, const BiteContext &context) const override {
        return context.random() < (cause == LossCause::PressedEarly ? lose_early_ : lose_escaped_);
    }

    [[nodiscard]] bool ChoosesForTheCast() const override { return true; }

private:
    static double Weight(Level level) {
        switch (level) {
            case Level::Low:
                return 0.5;
            case Level::Normal:
                return 1.0;
            case Level::High:
                return 1.5;
            case Level::None:
                break;
        }
        return 0.0;
    }

    static double Liking(const Species *species, const BiteContext &context) {
        return species != nullptr ? species->Affinity(context.bait, context.default_affinity) : 0.0;
    }

    const Span &NibbleFor(const FishView &fish, const BiteContext &context) const {
        switch (LevelOf(Liking(fish.species, context))) {
            case Level::High:
                return nibble_high_;
            case Level::Normal:
                return nibble_normal_;
            default:
                return nibble_low_;
        }
    }

    // Any fish in the water can come, as in Dark Cloud 2: the chosen one swims over in its wait. A bait
    // no fish here likes never brings one; the fish are looked at again now and then.
    void Roll(BiteSession &session, const BiteContext &context) const {
        std::vector<double> weights;
        double              liked = 0.0;
        for (const Species *species : context.fish) {
            weights.push_back(Weight(LevelOf(Liking(species, context))));
            liked += weights.back();
        }
        session = {};
        if (liked <= 0.0) {
            session.until = context.now + kLookAgain;
            return;
        }
        double roll = context.random() * (liked + nothing_);
        for (std::size_t index = 0; index < weights.size(); index++) {
            if (roll < weights[index]) {
                session.chosen = static_cast<int>(index);
                break;
            }
            roll -= weights[index];
        }
        if (session.chosen < 0) {
            session.until = context.now + retry_;
            return;
        }
        session.rolled = true;
    }

    Span   wait_;
    int    retry_ = 0;
    double nothing_ = 0.0;
    Span   nibble_low_;
    Span   nibble_normal_;
    Span   nibble_high_;
    Span   window_;
    double lose_early_ = 0.0;
    double lose_escaped_ = 0.0;
};

bool Flag(const Json &settings, const char *key, std::string_view where) {
    if (!settings.contains(key)) {
        return false;
    }
    if (!settings[key].is_boolean()) {
        throw std::runtime_error(std::format("{}: {} must be true or false", where, key));
    }
    return settings[key].get<bool>();
}

/**
 * The fish into the bag as its species' item, with its size; the points and the records only where
 * the place asks for them.
 */
class KeepOutcome final : public OutcomeStage {
public:
    explicit KeepOutcome(const Json &settings)
        : points_(Flag(settings, "points", "outcome")), records_(Flag(settings, "records", "outcome")) {}

    void Apply(Catch &fish, const OutcomeContext &context) const override {
        fish.kept = context.keep(fish);
        if (records_ && fish.species != nullptr && fish.species->mardan_garayan) {
            context.count_mardan_garayan();
        }
        if (points_) {
            context.add_points(fish.points);
        }
        if (records_) {
            context.record(fish);
        }
        // The window says what was given: no points unless the place gives them.
        Catch shown = fish;
        shown.points = points_ ? fish.points : 0;
        context.show(shown);
    }

private:
    bool points_;
    bool records_;
};

} // namespace

void RegisterBuiltinStages() {
    RegisterSpawnStage("retail", [](const Json &settings) { return std::make_unique<RetailSpawn>(settings); });
    RegisterBiteStage("retail", [](const Json &settings) { return std::make_unique<RetailBite>(settings); });
    RegisterBiteStage("brisk", [](const Json &settings) { return std::make_unique<BriskBite>(settings); });
    RegisterFightStage("retail", [](const Json &settings) { return std::make_unique<RetailFight>(settings); });
    RegisterOutcomeStage("retail", [](const Json &) { return std::make_unique<RetailOutcome>(); });
    RegisterOutcomeStage("keep", [](const Json &settings) { return std::make_unique<KeepOutcome>(settings); });
}

} // namespace dc::fishing
