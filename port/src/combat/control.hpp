#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "combat/hits.hpp"

/**
 * Enemy control (the combat framework's part 2, docs/COMBAT_FRAMEWORK.md). The framework takes a monster
 * from its script, plays a sequence of its own motions by name (a knockdown, a stagger), and hands it back:
 * the script then starts its loop (program 100) again, as it does after its hit handler. This header
 * keeps the sequence's state for each of CMonstorUnit's 16 slots; monstorunit.cpp plays what it says.
 * Like hits.hpp, it includes no PS2 headers.
 */
namespace dc::combat {

/**
 * The monster slots CMonstorUnit holds.
 */
inline constexpr int kMonsterSlots = 16;

/**
 * One motion of a reaction.
 */
struct MotionStep {
    std::string motion;         /**< The motion's name in the model's key table. */
    int         loop_ticks = 0; /**< 0 plays the motion once; otherwise it loops for this many ticks. */
    float       speed = -1.0f;  /**< Frames a tick; below 0, the key table's own. */
};

/**
 * What a monster does while the framework holds it.
 */
struct Reaction {
    std::string             name;    /**< "down" or "stagger", for the trace. */
    std::vector<MotionStep> steps;   /**< Played in order. */
    std::string             lock_on; /**< A frame the lock-on aims at meanwhile ("face"), or empty. */
};

/**
 * What the game does for a held monster this tick.
 */
enum class HoldAction : uint8_t {
    None,    /**< Nothing new: the step plays on. */
    Play,    /**< Start HoldTick::step. */
    Release, /**< The reaction is over: give the monster back to its script. */
};

/**
 * A tick of a hold.
 */
struct HoldTick {
    HoldAction  action = HoldAction::None;
    std::size_t step = 0; /**< The step to play, with HoldAction::Play. */
};

/**
 * Starts holding slot with reaction, replacing a hold it had. Returns its first step, or Release for a
 * reaction with none.
 */
HoldTick Hold(int slot, Reaction reaction);

/**
 * Whether slot is held.
 */
bool Held(int slot);

/**
 * The reaction slot is held with, or null.
 */
const Reaction *HeldReaction(int slot);

/**
 * One tick of slot's hold. motion_ended: the step's motion has reached its last frame. A step played
 * once ends there, but not on the tick it started, whose motion the character has yet to take up.
 */
HoldTick StepHold(int slot, bool motion_ended);

/**
 * Stops holding slot.
 */
void Release(int slot);

/**
 * Stops holding every slot: the floor's monsters are gone.
 */
void ReleaseAll();

/**
 * A knockdown from a model's key table: its heavy hit (damage_2), then its get-up (get_up). None when
 * it lacks either. Many models keep both keys as placeholders on their idle frames (the Dasher, the
 * Golem, the Lich...), which can't fall, so a key counts only with frames of its own: some, and not its
 * idle's (the table's first) or the other's.
 */
std::optional<Reaction> DownReaction(std::span<const MotionKey> keys);

/**
 * A stagger for a monster that can't fall: its own hit reaction, then a moment standing without
 * attacking. Master Utan (c14a) kneels and pants, as he does in retail when hit on a foot, with the
 * lock-on on his face. None when the model has no hit reaction.
 */
std::optional<Reaction> StaggerReaction(std::string_view model, std::span<const MotionKey> keys);

/**
 * DC_COMBAT_TEST: "down" has a finisher or a charged attack down a monster that can fall and stagger one
 * that can't, to see enemy control before poise decides it. Empty when unset.
 */
std::string_view TestMode();

} // namespace dc::combat
