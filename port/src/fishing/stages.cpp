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
    void Apply(const Catch &fish, const OutcomeContext &context) const override {
        if (fish.species != nullptr && fish.species->mardan_garayan) {
            context.count_mardan_garayan();
        }
        context.add_points(fish.points);
        if (fish.species != nullptr) {
            context.record(fish.species->kind, fish.size);
        }
        context.show(fish);
    }
};

} // namespace

void RegisterBuiltinStages() {
    RegisterSpawnStage("retail", [](const Json &settings) { return std::make_unique<RetailSpawn>(settings); });
    RegisterBiteStage("retail", [](const Json &settings) { return std::make_unique<RetailBite>(settings); });
    RegisterFightStage("retail", [](const Json &settings) { return std::make_unique<RetailFight>(settings); });
    RegisterOutcomeStage("retail", [](const Json &) { return std::make_unique<RetailOutcome>(); });
}

} // namespace dc::fishing
