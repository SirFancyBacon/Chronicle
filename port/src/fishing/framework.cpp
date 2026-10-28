#include "fishing/framework.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>

namespace dc::fishing {

namespace {

std::map<std::string, StageFactory<SpawnStage>, std::less<>> &SpawnStages() {
    static std::map<std::string, StageFactory<SpawnStage>, std::less<>> stages;
    return stages;
}

std::map<int, Species> &ByKind() {
    static std::map<int, Species> species;
    return species;
}

std::map<int, Place> &BySpot() {
    static std::map<int, Place> places;
    return places;
}

} // namespace

float Species::Affinity(std::string_view bait) const {
    const auto found = food_affinity.find(std::string(bait));
    return found != food_affinity.end() ? found->second : 0.0f;
}

void RegisterSpawnStage(std::string_view name, StageFactory<SpawnStage> factory) {
    SpawnStages().insert_or_assign(std::string(name), std::move(factory));
}

std::unique_ptr<SpawnStage> MakeSpawnStage(const nlohmann::json &config) {
    if (!config.is_object() || !config.contains("stage") || !config["stage"].is_string()) {
        throw std::runtime_error("a stage needs its name in \"stage\"");
    }
    const std::string name = config["stage"].get<std::string>();
    const auto        found = SpawnStages().find(name);
    if (found == SpawnStages().end()) {
        throw std::runtime_error(std::format("no spawn stage is called \"{}\"", name));
    }
    return found->second(config);
}

void Clear() {
    ByKind().clear();
    BySpot().clear();
}

void RegisterSpecies(Species species) {
    const int kind = species.kind;
    ByKind().insert_or_assign(kind, std::move(species));
}

void RegisterPlace(Place place) {
    const int spot = place.spot;
    BySpot().insert_or_assign(spot, std::move(place));
}

const Species *FindSpecies(std::string_view id) {
    const auto found = std::ranges::find(ByKind(), id, [](const auto &entry) { return std::string_view(entry.second.id); });
    return found != ByKind().end() ? &found->second : nullptr;
}

const Species *SpeciesOfKind(int kind) {
    const auto found = ByKind().find(kind);
    return found != ByKind().end() ? &found->second : nullptr;
}

const Place *PlaceAtSpot(int spot) {
    const auto found = BySpot().find(spot);
    return found != BySpot().end() ? &found->second : nullptr;
}

std::vector<const Species *> AllSpecies() {
    std::vector<const Species *> all;
    for (const auto &[kind, species] : ByKind()) {
        all.push_back(&species);
    }
    return all;
}

} // namespace dc::fishing
