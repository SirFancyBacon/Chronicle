#include "fishing/quests.hpp"

#include <algorithm>
#include <utility>

#include "quest/framework.hpp"

namespace dc::fishing {

bool CatchFilter::Matches(const Catch &fish) const {
    return (species.empty() || (fish.species != nullptr && fish.species->id == species)) &&
           (place.empty() || (fish.place != nullptr && fish.place->id == place)) && (bait.empty() || fish.bait == bait) &&
           fish.size >= min_size;
}

void CountCatches(std::string quest, std::string var, CatchFilter filter) {
    OnCatch([quest = std::move(quest), var = std::move(var), filter = std::move(filter)](const Catch &fish) {
        if (filter.Matches(fish)) {
            quest::ChangeVar(quest, var, [](int count) { return count + 1; });
        }
    });
}

void CountFishPoints(std::string quest, std::string var) {
    OnCatch([quest = std::move(quest), var = std::move(var)](const Catch &fish) {
        quest::ChangeVar(quest, var, [&fish](int points) { return points + fish.points; });
    });
}

void KeepLargest(std::string quest, std::string var, CatchFilter filter) {
    OnCatch([quest = std::move(quest), var = std::move(var), filter = std::move(filter)](const Catch &fish) {
        if (filter.Matches(fish)) {
            quest::ChangeVar(quest, var, [&fish](int largest) { return std::max(largest, fish.size); });
        }
    });
}

void CountSpecies(std::string quest, std::string var, std::vector<std::string> species) {
    OnCatch([quest = std::move(quest), var = std::move(var), species = std::move(species)](const Catch &fish) {
        if (fish.species == nullptr || std::ranges::find(species, fish.species->id) == species.end()) {
            return;
        }
        bool first = false;
        quest::ChangeVar(quest, var + "." + fish.species->id, [&first](int caught) {
            first = caught == 0;
            return 1;
        });
        if (first) {
            quest::ChangeVar(quest, var, [](int count) { return count + 1; });
        }
    });
}

} // namespace dc::fishing
