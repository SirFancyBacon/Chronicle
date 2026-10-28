#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/**
 * The fishing framework (docs/FISHING_FRAMEWORK.md): the species and places fishing knows, and the
 * stages that decide what happens at each, chosen by name in the places' documents. No game headers;
 * fishing/host.cpp fills retail's tables from it.
 */
namespace dc::fishing {

/**
 * A kind of fish.
 */
struct Species {
    std::string                  id;   /**< vanilla:fish_<kind> for retail's. */
    int                          kind; /**< The number CFish and the save's records know it by. */
    std::string                  model;
    int                          name_msg = 0; /**< The system message naming it. */
    float                        model_size = 1.0f; /**< The size the angling model is drawn at unscaled. */
    float                        min_size = 1.0f;   /**< The usual size, the centre of the roll. */
    float                        max_size = 2.0f;
    int                          min_fp = 0; /**< Fish points at min_size. */
    int                          max_fp = 0;
    std::map<std::string, float> food_affinity; /**< Liking by bait item ID; a bait left out is not liked. */
    bool                         mardan_garayan = false; /**< Counted toward the Mardan Garayan. */

    [[nodiscard]] float Affinity(std::string_view bait) const;
};

enum class TimeOfDay {
    Day,
    Evening,
    Night,
    Morning,
};

/**
 * What a spawn stage is given besides the place.
 */
struct SpawnContext {
    TimeOfDay               time = TimeOfDay::Day;
    std::function<double()> random; /**< Uniform in [0, 1). */
};

struct Place;

/**
 * Chooses the fish in the water when fishing starts at a place.
 */
class SpawnStage {
public:
    virtual ~SpawnStage() = default;

    /**
     * The species of each fish, place.fish_count of them.
     */
    virtual std::vector<const Species *> Spawn(const Place &place, const SpawnContext &context) const = 0;
};

/**
 * Makes a stage from its settings in a place's document; throws std::runtime_error when they are not
 * ones it takes.
 */
template <class Stage>
using StageFactory = std::function<std::unique_ptr<Stage>(const nlohmann::json &settings)>;

/**
 * Where fishing happens: one of the spots the town scripts open.
 */
struct Place {
    std::string                       id;
    int                               spot = 0; /**< The number the town script passes. */
    int                               fish_count = 0;
    bool                              draw_under_water = true; /**< The sea hides its fish. */
    std::shared_ptr<const SpawnStage> spawn;
};

/**
 * The most fish one place holds: retail's water has room for six.
 */
inline constexpr int kMaxFish = 6;

void RegisterSpawnStage(std::string_view name, StageFactory<SpawnStage> factory);

/**
 * The spawn stage a place names, made from its settings; throws std::runtime_error for a name no
 * stage has or settings it refuses.
 */
std::unique_ptr<SpawnStage> MakeSpawnStage(const nlohmann::json &config);

/**
 * Forgets every species and place.
 */
void Clear();

void RegisterSpecies(Species species);
void RegisterPlace(Place place);

const Species *FindSpecies(std::string_view id);
const Species *SpeciesOfKind(int kind);
const Place   *PlaceAtSpot(int spot);

/**
 * Every species, by kind.
 */
std::vector<const Species *> AllSpecies();

/**
 * Registers the stages the port ships.
 */
void RegisterBuiltinStages();

} // namespace dc::fishing
