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
 * What a fish is doing about the bait, as CFish's action holds it.
 */
enum class FishAction {
    None,     /**< Swimming; may notice the bait. */
    Approach, /**< Swimming to the bait. */
    Eat,      /**< Nibbling: the float bobs. */
    Bite,     /**< On the hook: the float goes under. */
    Leave,    /**< Swimming away. */
};

/**
 * A fish in the water as a bite stage sees it.
 */
struct FishView {
    const Species *species = nullptr;
    FishAction     action = FishAction::None;
    float          distance = 0.0f; /**< From the bait. */
};

/**
 * What a bite stage is given besides the fish.
 */
struct BiteContext {
    std::string_view        bait;          /**< The bait's item ID; empty for a bare hook. */
    float                   radius = 0.0f; /**< How far fish notice it. */
    std::function<double()> random;        /**< Uniform in [0, 1). */
};

/**
 * A fish's next action and the ticks until it is asked again.
 */
struct ActionStep {
    FishAction action;
    int        ticks;
};

/**
 * Why the bait may be lost.
 */
enum class LossCause {
    PressedEarly, /**< Cross while the fish only nibbled. */
    LetGo,        /**< The fish let go of the hook before Cross. */
};

/**
 * Decides what each fish does about the bait, and when the bait is lost.
 */
class BiteStage {
public:
    virtual ~BiteStage() = default;

    /**
     * A fish's next action, asked when its last one has run its ticks.
     */
    virtual ActionStep Update(const FishView &fish, const BiteContext &context) const = 0;

    /**
     * Whether the bait comes off for this cause.
     */
    virtual bool LosesBait(LossCause cause, const BiteContext &context) const = 0;
};

/**
 * What a fight stage is given each tick.
 */
struct FightContext {
    int                                           ticks = 0;   /**< Since the hook was set. */
    std::function<bool()>                         model_ready; /**< Whether the fish's angling model has loaded. */
    std::function<double()>                       random;
    std::function<void(int intensity, int ticks)> rumble;
};

enum class FightResult {
    Fighting,
    Landed,
};

/**
 * From the hook being set to the fish landed.
 */
class FightStage {
public:
    virtual ~FightStage() = default;

    virtual FightResult Step(const FightContext &context) const = 0;
};

/**
 * A landed fish.
 */
struct Catch {
    const Species *species = nullptr;
    int            size = 0;   /**< In tenths of a size unit, as the records keep it. */
    int            points = 0;
};

/**
 * What an outcome stage can give the player; the host's.
 */
struct OutcomeContext {
    std::function<void(int points)>              add_points;
    std::function<void(int kind, int size)>      record;
    std::function<void()>                        count_mardan_garayan;
    std::function<void(const Catch &)>           show; /**< The evaluation message. */
};

/**
 * What a landed fish gives the player.
 */
class OutcomeStage {
public:
    virtual ~OutcomeStage() = default;

    virtual void Apply(const Catch &fish, const OutcomeContext &context) const = 0;
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
    std::shared_ptr<const SpawnStage>   spawn;
    std::shared_ptr<const BiteStage>    bite;
    std::shared_ptr<const FightStage>   fight;
    std::shared_ptr<const OutcomeStage> outcome;
};

/**
 * The most fish one place holds: retail's water has room for six.
 */
inline constexpr int kMaxFish = 6;

void RegisterSpawnStage(std::string_view name, StageFactory<SpawnStage> factory);
void RegisterBiteStage(std::string_view name, StageFactory<BiteStage> factory);
void RegisterFightStage(std::string_view name, StageFactory<FightStage> factory);
void RegisterOutcomeStage(std::string_view name, StageFactory<OutcomeStage> factory);

/**
 * The stage a place names, made from its settings; throws std::runtime_error for a name no stage of
 * the kind has or settings it refuses.
 */
std::unique_ptr<SpawnStage>   MakeSpawnStage(const nlohmann::json &config);
std::unique_ptr<BiteStage>    MakeBiteStage(const nlohmann::json &config);
std::unique_ptr<FightStage>   MakeFightStage(const nlohmann::json &config);
std::unique_ptr<OutcomeStage> MakeOutcomeStage(const nlohmann::json &config);

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
