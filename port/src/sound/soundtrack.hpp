#pragma once

// The custom soundtrack (docs/SOUNDTRACK.md): recordings played in place of the game's sequences while Audio > Soundtrack is Custom.
// soundtrack/soundtrack.json, beside save/ and data/, names them by the sequences' own file names (DC_AUDIO_TRACE=1 prints them), times in
// seconds:
//   {"version": 1, "overrides": {"e01a_a.sq": {"file": "norune.ogg", "gain": 1.0, "loop": true, "loop_start": 12.5, "loop_end": 150.0}}}
// A recording is read and decoded on a worker of its own, so neither a tick nor the sound waits for it: asked for while it's still loading, the
// sequence plays, and the recording from the next time.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "noktus/audio.hpp"

namespace dc::sound {

/** A recording ready to play: stereo, at its file's own rate, its loop's marks in frames (loop_end 0, its end). */
struct Recording {
    std::shared_ptr<const noktus::audio::Samples> samples;
    std::size_t                                   loop_start = 0;
    std::size_t                                   loop_end = 0;
    bool                                          loop = true;
    float                                         gain = 1.0f;
};

/** Makes soundtrack/ and an empty soundtrack.json beside the saves where there's none; false if it can't. */
bool SoundtrackEnsure();

/** The recording for a sequence while the option is Custom, or null: the option is PS2, the sequence has no entry, its file is still loading,
 *  or won't decode. The first ask starts it loading. Two recordings are kept, at most 64 MiB of them. */
std::shared_ptr<const Recording> RecordingFor(std::string_view sequence);

/** A WAV, FLAC or Ogg Vorbis file's bytes (its extension one of theirs, ".ogg"), decoded by Noktus's audio as stereo: one channel doubled, more
 *  than two cut to the front pair. Null, with why in error, where they can't be decoded. */
std::shared_ptr<noktus::audio::Samples> DecodeRecording(std::span<const std::uint8_t> bytes, std::string_view extension, std::string &error);

} // namespace dc::sound
