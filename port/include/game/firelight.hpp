#pragma once

#include <cstdint>

namespace dc::game {

struct EDIT_EFFECT_INFO;

/**
 * @file
 * Fires as point lights, with modern light (step 7's point lights): the town's and the dungeons' fires lighting what's around them as light does.
 */

/**
 * Gives a fire's light for this tick, flickering gently as a fire does, where its flame burns; nothing without modern light. The game's average fire
 * brightness at fifteen units: nearer it's brighter, further fainter, as light falls off, and none past eighty units.
 * @param key Who the fire is from tick to tick, never 0: its flicker and its place between ticks follow it.
 * @param flame Where the flame burns, in the world.
 */
void GiveFireLight(std::uint64_t key, const float *flame);

/**
 * A dungeon's torch's light: as GiveFireLight, at a share of a town's, its torches hanging close to what they light.
 */
void GiveDungeonFireLight(std::uint64_t key, const float *flame);

/**
 * Gives a candle's light for this tick, as GiveFireLight does a fire's: a small flame, a share of a torch's light reaching less far.
 * @param key Who the candle is from tick to tick, never 0.
 * @param flame Where its flame burns, in the world.
 */
void GiveCandleLight(std::uint64_t key, const float *flame);

/**
 * Gives the light of an effect the game draws as a flame (EDIT_EFFECT_INFO), a fire or a lit candle, where it burns; nothing for other effects. The
 * effect's frame must stand where the game draws it from.
 */
void GiveEffectLight(const EDIT_EFFECT_INFO *effect);

/**
 * Gives one of the opening's fires as a light, as it's drawn this tick. Only a fire drawn whole (its flag 1) gives one: the lamps drawn as a halo alone
 * glow the magic's purple, which a flame's warm light would fight, and a fire the game has hidden far underground gives none.
 * @param index Its place in OP_FirePosition, which keys it.
 * @param pos Where it stands, as CFireOmni::pos holds it.
 * @param fire_flag Its OP_FireFlg.
 */
void GiveOpeningFireLight(int index, const float *pos, int fire_flag);

/**
 * Whether a fire's shimmer would have been laid back on the screen since this was last asked: CFireOmni::DrawRaster, where the renderer draws the heat
 * itself (render::ModernHeat) and the game's copy isn't laid back. Asking clears it.
 */
bool TakeShimmerShown();

/** How far above where a fire stands its flame burns, as CFireOmni draws it. */
inline constexpr float kFlameRise = 4.6f;

} // namespace dc::game
