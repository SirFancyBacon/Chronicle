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

#include "audio/mixer.hpp"
#include "audio/pcm.hpp"
#include "audio/soundtrack.hpp"
#include "audio/trace.hpp"
#include "audio/vag.hpp"
#include "noktus/audio.hpp"
#include "platform/audio.hpp"
#include "platform/clock.hpp"
#include "platform/settings.hpp"
#include "sound/published.hpp"

namespace dc::sound {

namespace na = noktus::audio;

namespace {

// The sound library's ports, as the game numbers them (game/sound.hpp, MIDI_PORT_*): the music's and the ambient loop's; the rest effects.
constexpr int kPorts = audio::kPorts;
constexpr int kMusicPort = 0;
constexpr int kAmbientPort = 1;

bool ValidPort(int port) { return port >= 0 && port < kPorts; }

} // namespace

Engine ActiveEngine() {
    static const Engine engine = [] {
        const char *asked = std::getenv("DC_AUDIO_ENGINE");
        return asked != nullptr && std::string_view(asked) == "kazarov" ? Engine::Kazarov : Engine::Noktus;
    }();
    return engine;
}

void *SoundBank::Address() const {
    return kazarov != nullptr ? static_cast<void *>(kazarov.get()) : const_cast<void *>(static_cast<const void *>(noktus.get()));
}

std::size_t SoundSequence::Songs() const { return kazarov != nullptr ? kazarov->Songs().size() : noktus.size(); }

void *SoundSequence::Address() const {
    return kazarov != nullptr ? static_cast<void *>(kazarov.get())
           : noktus.empty()   ? nullptr
                              : const_cast<void *>(static_cast<const void *>(noktus.front().get()));
}

std::shared_ptr<const na::Bank> NoktusBankFromRaw(std::span<const std::uint8_t> hd, std::span<const std::uint8_t> bd) {
    const std::optional<audio::HdBank> header = audio::HdBank::Parse(hd);
    if (!header) {
        return nullptr;
    }
    auto bank = std::make_shared<na::Bank>();
    for (const audio::HdProgram &p : header->programs) {
        na::BankProgram &program = bank->programs.emplace_back();
        program.present = p.present;
        program.transpose = p.transpose;
        program.detune = p.detune;
        program.volume = p.volume;
        program.pan = p.pan;
        for (const audio::HdSplit &s : p.splits) {
            program.splits.push_back({.key_low = s.key_low,
                                      .key_high = s.key_high,
                                      .sample_set = s.sample_set,
                                      .transpose = s.transpose,
                                      .detune = s.detune,
                                      .volume = s.volume,
                                      .pan = s.pan,
                                      .bend_low = s.bend_low,
                                      .bend_high = s.bend_high});
        }
    }
    for (const audio::HdSampleSet &s : header->sample_sets) {
        bank->sample_sets.push_back({.samples = s.samples, .velocity_low = s.vel_low, .velocity_high = s.vel_high, .velocity_curve = s.vel_curve});
    }
    for (const audio::HdSample &s : header->samples) {
        bank->samples.push_back({.wave = s.vag,
                                 .base_note = s.base_note,
                                 .detune = s.detune,
                                 .volume = s.volume,
                                 .pan = s.pan,
                                 .adsr1 = s.adsr1,
                                 .adsr2 = s.adsr2,
                                 .spu_attribute = s.spu_attr,
                                 .velocity_low = s.vel_low,
                                 .velocity_high = s.vel_high});
    }
    for (const audio::HdVagInfo &info : header->vags) {
        na::BankWave &wave = bank->waves.emplace_back();
        if (info.offset == audio::kUnused || info.offset >= bd.size() || info.rate <= 0) {
            continue;
        }
        const audio::VagSample vag = audio::DecodeVag(bd.subspan(info.offset));
        if (vag.pcm.empty()) {
            continue;
        }
        auto samples = std::make_shared<na::Samples>();
        samples->rate = info.rate;
        samples->data.reserve(vag.pcm.size());
        for (const std::int16_t v : vag.pcm) {
            samples->data.push_back(static_cast<float>(v) / 32768.0f);
        }
        wave.samples = std::move(samples);
        wave.loop_start = static_cast<std::size_t>(std::clamp(vag.loop_start, 0, static_cast<int>(vag.pcm.size()) - 1));
        wave.loops = vag.loops;
    }
    return bank;
}

std::shared_ptr<const na::Sequence> NoktusSequenceFromSq(const audio::SqFile &file, std::size_t song) {
    if (song >= file.Songs().size()) {
        return nullptr;
    }
    using Event = na::SequenceEvent;
    const audio::SqSong &source = file.Songs()[song];
    auto                 sequence = std::make_shared<na::Sequence>();
    sequence->division = source.division > 0 ? source.division : 480;
    std::uint32_t carried = 0; // the time of events passed over
    for (const audio::SqEvent &e : file.Events(source)) {
        const std::uint32_t delta = carried + e.delta;
        carried = 0;
        if (e.kind == audio::SqEventKind::Channel) {
            sequence->events.push_back({.delta = delta, .kind = Event::Kind::Channel, .status = e.status, .data1 = e.data[0], .data2 = e.data[1]});
        } else if (e.kind == audio::SqEventKind::Meta && e.meta == 0x51 && e.payload.size() >= 3) {
            sequence->events.push_back(
                {.delta = delta, .kind = Event::Kind::Tempo, .tempo = static_cast<std::uint32_t>(e.payload[0] << 16 | e.payload[1] << 8 | e.payload[2])});
        } else if (e.kind == audio::SqEventKind::End) {
            sequence->events.push_back({.delta = delta, .kind = Event::Kind::End});
        } else {
            carried = delta;
        }
    }
    return sequence;
}

SoundBank LoadSoundBank(const std::string &document, std::span<const std::uint8_t> hd, std::span<const std::uint8_t> bd) {
    SoundBank bank;
    if (ActiveEngine() == Engine::Kazarov) {
        bank.kazarov = document.empty() ? nullptr : LoadBank(document);
        if (!document.empty() && bank.kazarov == nullptr) {
            std::printf("sound: %s unreadable, playing the pack's own\n", document.c_str());
        }
        if (bank.kazarov == nullptr) {
            bank.kazarov = audio::Bank::Create(hd, bd);
        }
        return bank;
    }
    bank.noktus = document.empty() ? nullptr : LoadNoktusBank(document);
    if (!document.empty() && bank.noktus == nullptr) {
        std::printf("sound: %s unreadable, playing the pack's own\n", document.c_str());
    }
    if (bank.noktus == nullptr) {
        bank.noktus = NoktusBankFromRaw(hd, bd);
    }
    return bank;
}

SoundSequence LoadSoundSequence(const std::string &document, std::span<const std::uint8_t> sq) {
    SoundSequence sequence;
    if (ActiveEngine() == Engine::Kazarov) {
        sequence.kazarov = document.empty() ? nullptr : LoadSequence(document);
        if (!document.empty() && sequence.kazarov == nullptr) {
            std::printf("sound: %s unreadable, playing the pack's own\n", document.c_str());
        }
        if (sequence.kazarov == nullptr && !sq.empty()) {
            sequence.kazarov = audio::SqFile::Create(sq);
        }
        return sequence;
    }
    // Every song of it: a published document's, or the pack's own read through Kazarov's reader.
    if (!document.empty()) {
        for (int song = 0;; song++) {
            auto one = LoadNoktusSequence(document, song);
            if (one == nullptr) {
                break;
            }
            sequence.noktus.push_back(std::move(one));
        }
        if (sequence.noktus.empty()) {
            std::printf("sound: %s unreadable, playing the pack's own\n", document.c_str());
        }
    }
    if (sequence.noktus.empty() && !sq.empty()) {
        if (const auto file = audio::SqFile::Create(sq)) {
            for (std::size_t song = 0; song < file->Songs().size(); song++) {
                if (auto one = NoktusSequenceFromSq(*file, song)) {
                    sequence.noktus.push_back(std::move(one));
                }
            }
        }
    }
    return sequence;
}

namespace {

// Kazarov's mixer, as Chronicle played it before Noktus.
class KazarovBackend final : public Backend {
public:
    bool StartOutput() override { return AudioOutputStart(Mixer().Rate(), Render, nullptr); }

    void StopOutput() override { AudioOutputStop(); }

    void PauseOutput(bool paused) override { AudioOutputPause(paused); }

    void SetMasterGain(float gain) override { Mixer().SetMasterGain(gain); }

    void SetSurround(bool on, bool /*seven_one*/) override { AudioSetSurround(on); }

    void SetStereo(bool stereo) override { Mixer().SetStereo(stereo); }

    // Kazarov's mixer reads between samples its own way.
    void SetInterpolation(bool) override {}

    void BindBank(int port, const SoundBank &bank) override { Mixer().BindBank(port, bank.kazarov); }

    void SetSequence(int port, const SoundSequence &sequence) override { Mixer().SetSequence(port, sequence.kazarov); }

    void SetStream(int port, std::string_view sequence_name) override { Mixer().SetStream(port, audio::SoundtrackFor(sequence_name)); }

    void Rewind(int port, int song) override { Mixer().Rewind(port, song); }

    void Play(int port) override { Mixer().Play(port); }

    void Stop(int port) override { Mixer().Stop(port); }

    bool IsPlaying(int port) const override { return Mixer().IsPlaying(port); }

    void SetVolume(int port, int volume) override { Mixer().SetVolume(port, volume); }

    int Volume(int port) const override { return Mixer().Volume(port); }

    void SetAttribute(int port, int attribute) override { Mixer().SetAttribute(port, attribute); }

    void ShortMessage(int port, std::uint32_t message) override { Mixer().ShortMessage(port, message); }

    void HsMessage(int port, std::span<const std::uint8_t> message) override { Mixer().HsMessage(port, message); }

    void KeyOffCore(int core) override { Mixer().KeyOffCore(core); }

    void SetReverb(int core, int mode, int depth) override { Mixer().SetReverb(core, mode, depth); }

    // Kazarov's mixer pans by the game's pan alone.
    void SetListener(const float *, const float *) override {}

    void EffectPlace(int, int, int, const float *, float) override {}

    bool HearsPlaces() const override { return false; }

private:
    static audio::Mixer &Mixer() { return audio::DefaultMixer(); }

    static void Render(void *, float *out, int frames) { audio::DefaultMixer().Render(out, frames); }
};

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
            return false; // DC_AUDIO=off: no engine at all, so nothing waits on a render that never comes (as Kazarov's mixer is never pulled)
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
            // As Kazarov's capture did: rendered on the game's thread as the clock ticks, so N ticks write N / tick rate seconds however fast the run.
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
            players_[port]->SetBank(bank.noktus);
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
        players_[port]->SetSequence(sequence.noktus.empty() ? nullptr : sequence.noktus.front());
    }

    void SetStream(int port, std::string_view sequence_name) override {
        if (!ValidPort(port)) {
            return;
        }
        Port &state = ports_[port];
        StopRecording(state);
        state.track = audio::SoundtrackFor(sequence_name);
    }

    void Rewind(int port, int song) override {
        if (!ValidPort(port)) {
            return;
        }
        Port &state = ports_[port];
        if (song >= 0 && static_cast<std::size_t>(song) < state.sequence.noktus.size() && song != state.song) {
            state.song = song;
            players_[port]->SetSequence(state.sequence.noktus[static_cast<std::size_t>(song)]);
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
        if (state.samples == nullptr || state.converted != state.track.get()) {
            state.samples = ToSamples(*state.track);
            state.converted = state.track.get();
        }
        const audio::PcmTrack &track = *state.track;
        state.recording = engine_->Start(state.samples, {.bus = port == kMusicPort ? na::Bus::Music : na::Bus::Effects,
                                                         .placement = {.kind = na::Placement::Kind::Around},
                                                         .volume = RecordingVolume(state),
                                                         .loop = track.loop,
                                                         .start = static_cast<std::size_t>(std::max<std::int64_t>(track.start, 0)),
                                                         .loop_start = static_cast<std::size_t>(std::max<std::int64_t>(track.loop_start, 0)),
                                                         .loop_end = static_cast<std::size_t>(std::max<std::int64_t>(track.loop_end, 0))});
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
        std::shared_ptr<const audio::PcmTrack> track;
        const audio::PcmTrack                 *converted = nullptr;
        std::shared_ptr<na::Samples>           samples;
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

    static std::shared_ptr<na::Samples> ToSamples(const audio::PcmTrack &track) {
        auto samples = std::make_shared<na::Samples>();
        samples->rate = track.rate;
        samples->channels = 2;
        samples->data.resize(track.samples.size());
        for (std::size_t i = 0; i < track.samples.size(); i++) {
            samples->data[i] = static_cast<float>(track.samples[i]) / 32768.0f;
        }
        return samples;
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
    na::Interpolation                                       interpolation_ = na::Interpolation::Gaussian;
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
    static Backend *backend = ActiveEngine() == Engine::Kazarov ? static_cast<Backend *>(new KazarovBackend) : new NoktusBackend;
    return *backend;
}

} // namespace dc::sound
