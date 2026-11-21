#pragma once

// The game's own sound files read straight into Noktus's audio: a pack's HD (the bank's programs, sample sets and samples) and BD (its waveforms,
// in the SPU2's ADPCM), and its SQ (the songs, as Sony's MIDI). Played where a pack has no published documents (sound/published.hpp), and the
// documents' check against the disc (sound_publish_test).

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "noktus/sequence.hpp"
#include "noktus/synth.hpp"

namespace dc::sound {

/** A bank from its HD and BD: null where the HD has no programs, samples or waveform table. A waveform the BD doesn't hold is left without
 *  samples. */
std::shared_ptr<const noktus::audio::Bank> RetailBank(std::span<const std::uint8_t> hd, std::span<const std::uint8_t> bd);

/** Every song of an SQ, in the game's order: a song the file names no place for is there with no events, so the others keep their numbers.
 *  None where the SQ can't be read. */
std::vector<std::shared_ptr<const noktus::audio::Sequence>> RetailSequences(std::span<const std::uint8_t> sq);

/** One of the SPU2's waveforms, decoded from where it starts in a BD: to its last block (the one marked as the end), or the data's end. */
struct RetailWave {
    std::vector<float> samples; /**< At whole scale, -1 to 1. */
    std::size_t        loop_start = 0;
    bool               loops = false;
};
RetailWave DecodeRetailWave(std::span<const std::uint8_t> bd);

} // namespace dc::sound
