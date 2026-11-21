#pragma once

// A sound heard from several places at once (a town's torches, its river), split among a few voices where places are heard (surround): each
// voice the sound from one place. Retail plays one voice for them all, at their volumes summed and their pans averaged; split, they're as loud
// together as that one was.

#include <array>
#include <span>

namespace dc::sound {

/** A voice's share of a split sound: the place it sounds from (an index into the places given) and its volume. */
struct SplitVoice {
    int   place = 0;
    float volume = 0.0f;
};

/**
 * Splits a sound among voices (one for each of out, at most): its loudest places, together as loud as retail's one voice (the places' volumes
 * summed, at most whole) by power, as sounds heard apart add. The voices that were sounding (was: the place each took last time, voice by voice)
 * each keep to the chosen place nearest where it was, so a voice doesn't jump across the listener as the places' order changes; new ones take
 * the loudest left. Returns how many voices sound.
 */
int SplitAmong(std::span<const float> volumes, std::span<const std::array<float, 3>> positions, std::span<const std::array<float, 3>> was,
               std::span<SplitVoice> out);

} // namespace dc::sound
