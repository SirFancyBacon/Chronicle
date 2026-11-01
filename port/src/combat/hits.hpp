#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/**
 * What each hit is (the combat framework's part 1, roadmap/COMBAT_FRAMEWORK.md). The game's hit records
 * (CCollisionData's 96 slots) carry damage, owner and knockback but not which move made the hit: Toan's
 * finisher has the same attack_no as his other combo hits, and a monster's attack_no is the order its
 * script made its damage collisions in. A Hit is kept beside each slot to say so. Like
 * item/framework.hpp, this header includes no PS2 headers.
 */
namespace dc::combat {

/**
 * The hit records CCollisionData holds.
 */
inline constexpr int kHitSlots = 96;

/**
 * What dealt a hit.
 */
enum class Attacker : uint8_t {
    None,      /**< Not described: a hit no site has named. */
    Character, /**< A party member; Hit::who is the character, 0 to 5. */
    Monster,   /**< A monster; Hit::who is its slot. */
    Item,      /**< A thrown item or a gem. */
    Bomb,      /**< A bomb's blast. */
    Floor,     /**< A trap or the floor's own script. */
};

/**
 * The move that made a hit.
 */
enum class Move : uint8_t {
    Unknown,  /**< Not described. */
    Combo,    /**< A step of a combo; Hit::step counts from 1. */
    Finisher, /**< The combo's last step; Hit::step is its place in the combo. */
    Charged,  /**< A charged attack; Hit::step is the charge level, from 1. */
    Shot,     /**< A shot, a spell or a weapon's effect. */
    Thrown,   /**< A thrown item. */
    Blast,    /**< A bomb's blast. */
    Trap,     /**< A trap. */
    Attack,   /**< A monster's attack; Hit::frame names the collision it rides on. */
};

/**
 * One hit's description.
 */
struct Hit {
    Attacker    attacker = Attacker::None;
    int         who = -1; /**< The character (0 to 5) or the monster's slot, or -1. */
    Move        move = Move::Unknown;
    int         step = 0;       /**< The combo step or charge level, or 0. */
    std::string model;          /**< A monster's model ("e03a"), to name its motions. */
    int         motion = -1;    /**< The motion a monster was playing, by its key table's number. */
    std::string frame;          /**< The frame a monster's damage collision rides on, or "shot". */
    float       poise = 0.0f;   /**< Poise damage it deals. */
    float       stamina = 0.0f; /**< Stamina damage it deals to a guard. */
};

/**
 * The poise and stamina damage a described hit deals.
 */
struct HitValues {
    float poise = 0.0f;
    float stamina = 0.0f;
};

/**
 * Gives a hit's values from what it is. The documents fill this later (parts 3 and 4); with none set,
 * every hit's values are 0.
 */
using ValueSource = std::function<HitValues(const Hit &hit)>;

/**
 * Sets where hits' values come from; an empty source gives 0.
 */
void SetValueSource(ValueSource source);

/**
 * Forgets slot's description: the slot holds a new hit.
 */
void Reset(int slot);

/**
 * Describes the hit in slot, filling its values from the source.
 */
void Describe(int slot, Hit hit);

/**
 * The description of the hit in slot; one no site described has Attacker::None. Null for a slot out
 * of range.
 */
const Hit *Find(int slot);

/**
 * A monster attack's key, "<motion>/<frame>", naming the motion by names (the model's key table, in
 * order) or by number where names has none; frame alone with no motion.
 */
std::string AttackKey(int motion, std::string_view frame, std::span<const std::string> names);

/**
 * A motion in a model's key table.
 */
struct MotionKey {
    std::string name;
    int         start = 0; /**< Its first frame. */
    int         end = 0;   /**< The frame after its last. */
    float       speed = 0; /**< Frames a tick, as the model plays it; 0 when the table doesn't say. */
};

/**
 * A monster's motions in its key table's order, which is how its script numbers them: the KEY commands
 * of its model's extracted sidecar (dun/monstor/<model>.game.json), read once a model. Retail's own model
 * script numbers its motions only. Empty when the data has no sidecar.
 */
const std::vector<MotionKey> &MotionKeys(const std::string &model);

/**
 * The names of MotionKeys(model), in the same order.
 */
const std::vector<std::string> &MotionNames(const std::string &model);

/**
 * The number of model's motion called name, or -1.
 */
int MotionIndex(const std::string &model, std::string_view name);

/**
 * A monster attack's key, naming the motion from its model's key table in the data.
 */
std::string AttackKey(const Hit &hit);

/**
 * A party member's name for a trace: "Toan" for 0, up to "Osmond" for 5.
 */
std::string_view CharacterName(int character);

/**
 * The hit for a trace: "Toan finisher (5)", "e03a[3] attack_1/dcol0".
 */
std::string Name(const Hit &hit);

/**
 * Whether DC_COMBAT_TRACE is set.
 */
bool Tracing();

/**
 * DC_COMBAT_TRACE=1: reports line, something combat did ("e03a[3] down").
 */
void Trace(std::string_view line);

/**
 * DC_COMBAT_TRACE=1: reports the hit in slot landing on target for damage.
 */
void TraceLanded(int slot, std::string_view target, int damage);

} // namespace dc::combat
