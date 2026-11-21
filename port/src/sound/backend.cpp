// nlohmann/json, through published.hpp's neighbours, before anything with the game's one-letter macros.
#include "sound/backend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>

#include "noktus/audio.hpp"
#include "platform/audio.hpp"
#include "platform/clock.hpp"
#include "platform/settings.hpp"
#include "sound/published.hpp"
#include "sound/retail.hpp"
#include "sound/soundtrack.hpp"

namespace dc::sound {

namespace na = noktus::audio;

namespace {

// The music's port and the ambient loop's, as the game numbers them (game/sound.hpp, MIDI_PORT_*); the rest are effects.
constexpr int kMusicPort = 0;
constexpr int kAmbientPort = 1;

bool ValidPort(int port) { return port >= 0 && port < kPorts; }

} // namespace

void *SoundBank::Address() const {
    return const_cast<void *>(static_cast<const void *>(bank.get()));
}

void *SoundSequence::Address() const {
    return songs.empty() ? nullptr : const_cast<void *>(static_cast<const void *>(songs.front().get()));
}

SoundBank LoadSoundBank(const std::string &document, std::span<const std::uint8_t> hd, std::span<const std::uint8_t> bd) {
    SoundBank bank;
    bank.bank = document.empty() ? nullptr : LoadNoktusBank(document);
    if (!document.empty() && bank.bank == nullptr) {
        std::printf("sound: %s unreadable, playing the pack's own\n", document.c_str());
    }
    if (bank.bank == nullptr) {
        bank.bank = RetailBank(hd, bd);
    }
    return bank;
}

SoundSequence LoadSoundSequence(const std::string &document, std::span<const std::uint8_t> sq) {
    // Every song of it: a published document's, or the pack's own.
    SoundSequence sequence;
    if (!document.empty()) {
        for (int song = 0;; song++) {
            auto one = LoadNoktusSequence(document, song);
            if (one == nullptr) {
                break;
            }
            sequence.songs.push_back(std::move(one));
        }
        if (sequence.songs.empty()) {
            std::printf("sound: %s unreadable, playing the pack's own\n", document.c_str());
        }
    }
    if (sequence.songs.empty() && !sq.empty()) {
        sequence.songs = RetailSequences(sq);
    }
    return sequence;
}

namespace {

// Noktus's audio engine: each port a sequence player (its bank, its song, its effects), a recording of the custom soundtrack in a sequence's
// place as a sound of its own, the output eased by the limiter (the owner's choice).
class NoktusBackend final : public Backend {
public:
    NoktusBackend() {
        for (int port = 0; port < kPorts; port++) {
            players_[port] = std::make_shared<na::SequencePlayer>();
            players_[port]->SetVolume(0.0f); // a port starts silent, as the driver's do (Port::volume)
            players_[port]->SetBus(port == kMusicPort ? na::Bus::Music : port == kAmbientPort ? na::Bus::Ambience
                                                                                              : na::Bus::Effects);
        }
    }

    bool StartOutput() override {
        if (engine_ != nullptr) {
            return engine_->HasDevice() || capturing_;
        }
        const char *wav = SettingsEnv("AUDIO_WAV");
        const char *audio = SettingsEnv("AUDIO");
        capturing_ = wav != nullptr && *wav != '\0';
        const bool device = !capturing_ && !(audio != nullptr && std::strcmp(audio, "off") == 0);
        if (!device && !capturing_) {
            return false; // DC_AUDIO=off: no engine at all, so nothing waits on a render that never comes
        }
        engine_ = na::AudioEngine::Create({.sample_rate = kRate, .speakers = Speakers(), .device = device, .limiter = true});
        engine_->SetMasterVolume(master_);
        engine_->SetListener(listener_);
        engine_->SetInterpolation(interpolation_);
        for (const auto &player : players_) {
            engine_->Attach(player);
        }
        ApplyReverbs();
        if (capturing_) {
            // Rendered on the game's thread as the clock ticks, so N ticks write N / tick rate seconds however fast the run.
            if (!engine_->StartCapture(wav)) {
                std::printf("sound: cannot write %s\n", wav);
                capturing_ = false;
                return false;
            }
            last_tick_ = ClockTickCount();
            ClockAddPumpHook(Pump);
            capture_ = this;
            return true;
        }
        if (device && !engine_->HasDevice()) {
            std::printf("sound: no playback device\n");
        }
        return engine_->HasDevice();
    }

    void StopOutput() override {
        if (capturing_) {
            ClockRemovePumpHook(Pump);
            engine_->StopCapture();
            capturing_ = false;
            capture_ = nullptr;
        }
        engine_.reset();
        for (Port &state : ports_) {
            state.recording = na::SoundId::None; // the next engine's sounds are numbered afresh
            state.held = false;
        }
    }

    void PauseOutput(bool paused) override {
        if (engine_ != nullptr) {
            engine_->Pause(paused);
        }
    }

    void SetMasterGain(float gain) override {
        master_ = gain;
        if (engine_ != nullptr) {
            engine_->SetMasterVolume(gain);
        }
    }

    void SetSurround(bool on, bool seven_one) override {
        surround_ = on;
        seven_one_ = seven_one;
        ApplySpeakers();
    }

    void SetStereo(bool stereo) override {
        stereo_ = stereo;
        ApplySpeakers();
    }

    void SetInterpolation(bool cubic) override {
        interpolation_ = cubic ? na::Interpolation::Cubic : na::Interpolation::Gaussian;
        if (engine_ != nullptr) {
            engine_->SetInterpolation(interpolation_);
        }
    }

    void BindBank(int port, const SoundBank &bank) override {
        if (ValidPort(port)) {
            players_[port]->SetBank(bank.bank);
        }
    }

    void SetSequence(int port, const SoundSequence &sequence) override {
        if (!ValidPort(port)) {
            return;
        }
        Port &state = ports_[port];
        state.sequence = sequence;
        state.song = 0;
        state.track = nullptr;
        StopRecording(state);
        players_[port]->SetSequence(sequence.songs.empty() ? nullptr : sequence.songs.front());
    }

    void SetStream(int port, std::string_view sequence_name) override {
        if (!ValidPort(port)) {
            return;
        }
        Port &state = ports_[port];
        StopRecording(state);
        state.track = RecordingFor(sequence_name);
    }

    void Rewind(int port, int song) override {
        if (!ValidPort(port)) {
            return;
        }
        Port &state = ports_[port];
        if (song >= 0 && static_cast<std::size_t>(song) < state.sequence.songs.size() && song != state.song) {
            state.song = song;
            players_[port]->SetSequence(state.sequence.songs[static_cast<std::size_t>(song)]);
        } else {
            players_[port]->Rewind();
        }
        StopRecording(state); // a recording starts again from its start at the next Play
    }

    void Play(int port) override {
        if (!ValidPort(port)) {
            return;
        }
        Port &state = ports_[port];
        if (state.track == nullptr) {
            players_[port]->Play();
            return;
        }
        if (engine_ == nullptr) {
            return;
        }
        if (state.recording != na::SoundId::None) {
            // Stopped and played again (SQ_RePlay): it carries on from where it stood, as the driver's stream does; one played to its end stays
            // there until rewound (Rewind forgets it, to start afresh).
            if (state.held) {
                engine_->Hold(state.recording, false);
                state.held = false;
            }
            return;
        }
        // The recording in the sequence's place: stereo all around as music is, at its gain on the port's volume, looping between its marks.
        const Recording &track = *state.track;
        state.recording = engine_->Start(track.samples, {.bus = port == kMusicPort ? na::Bus::Music : na::Bus::Effects,
                                                         .placement = {.kind = na::Placement::Kind::Around},
                                                         .volume = RecordingVolume(state),
                                                         .loop = track.loop,
                                                         .start = 0,
                                                         .loop_start = track.loop_start,
                                                         .loop_end = track.loop_end});
    }

    void Stop(int port) override {
        if (!ValidPort(port)) {
            return;
        }
        players_[port]->Stop();
        // A recording is held where it stands, to carry on at the next Play; a new song, a rewind or another recording forgets it.
        Port &state = ports_[port];
        if (engine_ != nullptr && state.recording != na::SoundId::None && !state.held) {
            engine_->Hold(state.recording, true);
            state.held = true;
        }
    }

    bool IsPlaying(int port) const override {
        if (!ValidPort(port)) {
            return false;
        }
        return players_[port]->Playing() || (engine_ != nullptr && !ports_[port].held && engine_->Playing(ports_[port].recording));
    }

    void SetVolume(int port, int volume) override {
        if (!ValidPort(port)) {
            return;
        }
        Port &state = ports_[port];
        state.volume = volume;
        players_[port]->SetVolume(static_cast<float>(std::clamp(volume, 0, kWholeVolume)) / static_cast<float>(kWholeVolume));
        if (engine_ != nullptr && state.recording != na::SoundId::None) {
            engine_->SetVolume(state.recording, RecordingVolume(state));
        }
    }

    int Volume(int port) const override { return ValidPort(port) ? ports_[port].volume : 0; }

    void SetAttribute(int port, int attribute) override {
        if (ValidPort(port)) {
            ports_[port].attribute = attribute; // kept, as the driver kept it, unused
        }
    }

    void ShortMessage(int port, std::uint32_t message) override {
        if (ValidPort(port)) {
            players_[port]->Message(static_cast<std::uint8_t>(message & 0xFF), static_cast<std::uint8_t>((message >> 8) & 0x7F),
                                    static_cast<std::uint8_t>((message >> 16) & 0x7F));
        }
    }

    // The hardware synth's messages: F9 the port's effect volume (0) or pan (1); FD a key on or off (10, its velocity), an instance's volume (00),
    // pan (01) or pitch (02, fourteen bits); an instance by its id and number, on the port's channel 0 program.
    void HsMessage(int port, std::span<const std::uint8_t> message) override {
        if (!ValidPort(port) || message.size() < 4) {
            return;
        }
        na::SequencePlayer &player = *players_[port];
        if (message[0] == 0xF9) {
            if (message[1] == 0) {
                player.EffectPortVolume(message[3] & 0x7F);
            } else if (message[1] == 1) {
                player.EffectPortPan(message[3] & 0x7F);
            }
            return;
        }
        if (message[0] != 0xFD || message.size() < 6) {
            return;
        }
        const int id = message[3] & 0x7F;
        const int number = message[4] & 0x7F;
        const int value = message[5] & 0x7F;
        switch (message[1]) {
            case 0x10:
                player.EffectOn(id, number, value);
                return;
            case 0x00:
                player.EffectVolume(id, number, value);
                return;
            case 0x01:
                player.EffectPan(id, number, value);
                return;
            case 0x02:
                if (message.size() >= 7) {
                    player.EffectBend(id, number, value | (message[6] & 0x7F) << 7);
                }
                return;
            default:
                return;
        }
    }

    void KeyOffCore(int core) override {
        if (engine_ != nullptr && core >= 0) {
            engine_->ReleasePool(static_cast<std::size_t>(core));
        }
    }

    // As the game sets a core's (CSound::SetReverb): its mode, and EVOL its depth shifted up eight bits, a signed register.
    void SetReverb(int core, int mode, int depth) override {
        if (core < 0 || core >= static_cast<int>(reverbs_.size())) {
            return;
        }
        reverbs_[static_cast<std::size_t>(core)] = {mode, depth};
        if (engine_ != nullptr) {
            engine_->SetReverb(static_cast<std::size_t>(core), static_cast<na::ReverbMode>(std::clamp(mode, 0, 9)),
                               static_cast<float>(static_cast<std::int16_t>((depth << 8) & 0xFFFF)) / 32768.0f);
        }
    }

    void SetListener(const float *position, const float *direction) override {
        const std::optional<na::Listener> listener = ListenerFor(position, direction);
        if (!listener) {
            return; // looking straight up or down: as it was
        }
        listener_ = *listener;
        if (engine_ != nullptr) {
            engine_->SetListener(listener_);
        }
    }

    void EffectPlace(int port, int id, int number, const float *position, float size) override {
        if (!ValidPort(port)) {
            return;
        }
        std::optional<na::Point> at;
        if (position != nullptr) {
            at = na::Point{position[0], position[1], position[2]};
        }
        players_[port]->EffectPlace(id & 0x7F, number & 0x7F, at, size);
    }

    bool HearsPlaces() const override {
        const na::Speakers speakers = Speakers();
        return speakers == na::Speakers::Surround51 || speakers == na::Speakers::Surround71;
    }

private:
    static constexpr int kRate = 48000;
    static constexpr int kWholeVolume = 256;

    struct Port {
        int                                    volume = 0;
        int                                    attribute = 0;
        SoundSequence                          sequence;
        int                                    song = 0;
        std::shared_ptr<const Recording>       track; // the custom soundtrack's, played in the sequence's place
        na::SoundId                            recording = na::SoundId::None;
        bool                                   held = false; // stopped, to carry on (Stop)
    };

    struct Reverb {
        int mode = 0;
        int depth = 0;
    };

    float RecordingVolume(const Port &state) const {
        const float gain = state.track != nullptr ? state.track->gain : 1.0f;
        return gain * static_cast<float>(std::clamp(state.volume, 0, kWholeVolume)) / static_cast<float>(kWholeVolume);
    }

    void StopRecording(Port &state) {
        if (engine_ != nullptr && state.recording != na::SoundId::None) {
            engine_->Stop(state.recording);
        }
        state.recording = na::SoundId::None;
        state.held = false;
    }

    na::Speakers Speakers() const {
        if (!stereo_) {
            return na::Speakers::Mono;
        }
        if (!surround_) {
            return na::Speakers::Stereo;
        }
        return seven_one_ ? na::Speakers::Surround71 : na::Speakers::Surround51;
    }

    void ApplySpeakers() {
        if (engine_ != nullptr && engine_->GetSpeakers() != Speakers()) {
            engine_->SetSpeakers(Speakers());
        }
    }

    void ApplyReverbs() {
        for (std::size_t core = 0; core < reverbs_.size(); core++) {
            SetReverb(static_cast<int>(core), reverbs_[core].mode, reverbs_[core].depth);
        }
    }

    // On the game's thread as the clock ticks, while capturing: the frames owed, rendered.
    static void Pump() {
        NoktusBackend *self = capture_;
        if (self == nullptr || self->engine_ == nullptr) {
            return;
        }
        const std::int64_t tick = ClockTickCount();
        if (tick > self->last_tick_) {
            self->owed_ += static_cast<double>(tick - self->last_tick_) * kRate / ClockTickRate();
        }
        self->last_tick_ = tick;
        auto frames = static_cast<std::size_t>(self->owed_);
        self->owed_ -= static_cast<double>(frames);
        const std::size_t channels = na::Channels(self->engine_->GetSpeakers());
        while (frames > 0) {
            const std::size_t count = std::min<std::size_t>(frames, 1024);
            self->chunk_.resize(count * channels);
            self->engine_->Render(self->chunk_);
            frames -= count;
        }
    }

    static inline NoktusBackend *capture_ = nullptr;

    std::unique_ptr<na::AudioEngine>                        engine_;
    std::array<std::shared_ptr<na::SequencePlayer>, kPorts> players_;
    std::array<Port, kPorts>                                ports_;
    std::array<Reverb, 2>                                   reverbs_{};
    na::Listener                                            listener_;
    float                                                   master_ = 1.0f;
    na::Interpolation                                       interpolation_ = na::Interpolation::Cubic;
    bool                                                    surround_ = false;
    bool                                                    seven_one_ = false;
    bool                                                    stereo_ = true;
    bool                                                    capturing_ = false;
    std::int64_t                                            last_tick_ = 0;
    double                                                  owed_ = 0.0;
    std::vector<float>                                      chunk_;
};

} // namespace

std::optional<na::Listener> ListenerFor(const float *position, const float *direction) {
    const float length = std::hypot(direction[0], direction[2]);
    if (!(length > 1e-6f) || !std::isfinite(length)) {
        return std::nullopt;
    }
    const na::Point forward = {direction[0] / length, 0.0f, direction[2] / length};
    // SndGetVolPan's right vector is (z, 0, -x) and it pans to the side its dot product is negative on: the right is that negated.
    return na::Listener{
        .position = {position[0], position[1], position[2]},
        .forward = forward,
        .right = {-forward[2], 0.0f,        forward[0] }
    };
}

Backend &Audio() {
    // Never destroyed: the output's thread may still be pulling from it while static destructors run.
    static Backend *backend = new NoktusBackend;
    return *backend;
}

} // namespace dc::sound

// Kazarov's window pauses the sound while the game is in the background (platform/audio.hpp's one call left in use).
void AudioOutputPause(bool paused) {
    dc::sound::Audio().PauseOutput(paused);
}
