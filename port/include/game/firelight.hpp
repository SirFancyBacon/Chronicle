#pragma once

#include <cstdint>

namespace dc::game {

/**
 * @file
 * Fires as point lights, with modern light (step 7's point lights): the town's and the dungeons' fires lighting what's around them as light does.
 */

/**
 * Gives a fire's light for this tick, flickering as a fire does, where its flame burns; nothing without modern light. Matched to the light the game gave
 * a character ten units from a town fire: nearer it's brighter, further fainter, as light falls off, and none past forty units, as the game's.
 * @param key Who the fire is from tick to tick, never 0: its flicker and its place between ticks follow it.
 * @param flame Where the flame burns, in the world.
 */
void GiveFireLight(std::uint64_t key, const float *flame);

/** How far above where a fire stands its flame burns, as CFireOmni draws it. */
inline constexpr float kFlameRise = 4.6f;

} // namespace dc::game
