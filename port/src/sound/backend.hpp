#pragma once

// The game's sound, played by Noktus's audio engine (Noktus's AUDIO_PLAN.md, slice 7). CSound and the EZMIDI answers speak to the engine through
// this, in the sound library's terms: sixteen ports, each a bank, a sequence or a recording in its place, effects, a volume.

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "noktus/audio.hpp"
#include "noktus/sequence.hpp"
#include "noktus/synth.hpp"

namespace dc::sound {

/** The sound library's ports, as the game numbers them (game/sound.hpp, MIDI_PORT_*). */
inline constexpr int kPorts = 16;

/** A bank for the engine. */
struct SoundBank {
    std::shared_ptr<const noktus::audio::Bank> bank;

    explicit operator bool() const { return bank != nullptr; }

    /** Where it is, for the game's state to hold: only ever compared with null. */
    void *Address() const;
};

/** A sequence, each of its songs, for the engine. */
struct SoundSequence {
    std::vector<std::shared_ptr<const noktus::audio::Sequence>> songs;

    explicit operator bool() const { return !songs.empty(); }

    std::size_t Songs() const { return songs.size(); }
    void       *Address() const;
};

/** A bank: its published document where there is one (empty, none), else the pack's own HD and BD. */
SoundBank LoadSoundBank(const std::string &document, std::span<const std::uint8_t> hd, std::span<const std::uint8_t> bd);

/** A sequence: its published document where there is one (empty, none), else the pack's own SQ. */
SoundSequence LoadSoundSequence(const std::string &document, std::span<const std::uint8_t> sq);

/** Who hears the world from a camera's place and the way it looks (SndSetCamera's): forward its look on the level, right the side SndGetVolPan
 *  pans a sound to the right of. None where it looks straight up or down. */
std::optional<noktus::audio::Listener> ListenerFor(const float *position, const float *direction);

/** What the game's sound asks of the engine. Its calls come from the game's thread. */
class Backend {
public:
    virtual ~Backend() = default;

    /** Opens the output (the device, or the WAV capture that DC_AUDIO_WAV asks for); false if there's none, the game playing silently. */
    virtual bool StartOutput() = 0;
    virtual void StopOutput() = 0;
    virtual void PauseOutput(bool paused) = 0;
    virtual void SetMasterGain(float gain) = 0;
    /** Surround on or off; seven_one, for 7.1 speakers rather than 5.1. */
    virtual void SetSurround(bool on, bool seven_one) = 0;
    /** Stereo, or mono (the game's sound option). */
    virtual void SetStereo(bool stereo) = 0;
    /** How samples are read between: the SPU2's Gaussian, or a cubic (the interpolation option). */
    virtual void SetInterpolation(bool cubic) = 0;

    virtual void BindBank(int port, const SoundBank &bank) = 0;
    /** The port's sequence, stopped and rewound; its song's notes let go of, any recording in its place forgotten. */
    virtual void SetSequence(int port, const SoundSequence &sequence) = 0;
    /** The custom soundtrack's recording for the sequence named, played in its place from the next Play; none, the sequence itself. */
    virtual void SetStream(int port, std::string_view sequence_name) = 0;
    virtual void Rewind(int port, int song) = 0;
    virtual void Play(int port) = 0;
    virtual void Stop(int port) = 0;
    virtual bool IsPlaying(int port) const = 0;
    /** 0 to 256, whole. */
    virtual void SetVolume(int port, int volume) = 0;
    virtual int  Volume(int port) const = 0;
    virtual void SetAttribute(int port, int attribute) = 0;
    /** A channel message: status in the low byte, then two data bytes. */
    virtual void ShortMessage(int port, std::uint32_t message) = 0;
    /** One of the hardware synth's effect messages (F9 port volume and pan, FD key, volume, pan and pitch). */
    virtual void HsMessage(int port, std::span<const std::uint8_t> message) = 0;
    virtual void KeyOffCore(int core) = 0;
    virtual void SetReverb(int core, int mode, int depth) = 0;

    /** Who hears the world's sounds: the camera's place and the way it looks (SndSetCamera's), on the level. */
    virtual void SetListener(const float *position, const float *direction) = 0;
    /** Where an effect instance (its id and number, under the port's program) sounds in the world: three numbers, or null for in front by its
     *  pan. Kept for the instance's next key on; within size of the listener it's heard in front by its pan. */
    virtual void EffectPlace(int port, int id, int number, const float *position, float size) = 0;
    /** Whether a sound's place is heard (surround); where it isn't, the game's own pan is all there is. */
    virtual bool HearsPlaces() const = 0;
};

/** The engine's. */
Backend &Audio();

} // namespace dc::sound
