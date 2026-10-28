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

} // namespace

void RegisterBuiltinStages() {
    RegisterSpawnStage("retail", [](const Json &settings) { return std::make_unique<RetailSpawn>(settings); });
}

} // namespace dc::fishing
