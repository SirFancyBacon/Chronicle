// nlohmann/json before the game headers, whose one-letter macros it cannot stand (gtest is none).
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "audio/hdbank.hpp"
#include "audio/mixer.hpp"
#include "audio/sq.hpp"
#include "noktus/audio.hpp"
#include "noktus/sequence.hpp"
#include "sound/published.hpp"

// Noktus's sequencer (its audio plan's slice 3) against Kazarov's, which Chronicle plays today: sequence documents read for Noktus, and the
// game's music played through both.
namespace {

namespace fs = std::filesystem;
using namespace dc;
using Kind = noktus::audio::SequenceEvent::Kind;

TEST(NoktusSequence, ReadsASequenceDocument) {
    const std::string text = R"({"kind": "sequence", "schema_version": 1, "songs": [{"division": 240, "events": [
        {"delta": 0, "kind": "meta", "meta": 81, "payload": [7, 161, 32]},
        {"delta": 10, "kind": "channel", "status": 176, "data": [7, 100]},
        {"delta": 5, "kind": "meta", "meta": 47, "payload": []},
        {"delta": 7, "kind": "channel", "status": 144, "data": [60, 90]},
        {"delta": 3, "kind": "end"}]}]})";
    const auto        song = sound::NoktusSequenceFromJson(text);
    ASSERT_NE(song, nullptr);
    EXPECT_EQ(song->division, 240);
    ASSERT_EQ(song->events.size(), 4u);
    EXPECT_EQ(song->events[0].kind, Kind::Tempo);
    EXPECT_EQ(song->events[0].tempo, 500000u);
    EXPECT_EQ(song->events[1].kind, Kind::Channel);
    EXPECT_EQ(song->events[1].status, 176);
    EXPECT_EQ(song->events[1].data2, 100);
    // The meta event it passes over gives its time to the note after it.
    EXPECT_EQ(song->events[2].delta, 12u);
    EXPECT_EQ(song->events[2].data1, 60);
    EXPECT_EQ(song->events[3].kind, Kind::End);
    EXPECT_EQ(sound::NoktusSequenceFromJson(text, 1), nullptr);
    EXPECT_EQ(sound::NoktusSequenceFromJson(R"({"kind": "sequence", "songs": [{"events": [{"delta": "x", "kind": "end"}]}]})"), nullptr);
    EXPECT_EQ(sound::NoktusSequenceFromJson(R"({"kind": "bank"})"), nullptr);
    EXPECT_EQ(sound::NoktusSequenceFromJson(R"({"kind": "sequence", "songs": [{"events": [{"delta": -1, "kind": "end"}]}]})"), nullptr);
    // A program change written with its one data byte.
    const auto one = sound::NoktusSequenceFromJson(R"({"kind": "sequence", "songs": [{"events": [{"delta": 0, "kind": "channel", "status": 192, "data": [5]}]}]})");
    ASSERT_NE(one, nullptr);
    ASSERT_EQ(one->events.size(), 1u);
    EXPECT_EQ(one->events[0].data1, 5);
}

std::optional<std::vector<std::uint8_t>> ReadFile(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {});
}

std::string Text(const fs::path &path) {
    const auto bytes = ReadFile(path);
    return bytes ? std::string(bytes->begin(), bytes->end()) : std::string();
}

// Each side's loudness in 10 ms windows: when notes come and go.
std::vector<double> Envelope(const std::vector<float> &stereo) {
    constexpr std::size_t kWindow = 480;
    std::vector<double>   out;
    for (std::size_t at = 0; at + kWindow * 2 <= stereo.size(); at += kWindow * 2) {
        double sum = 0.0;
        for (std::size_t i = 0; i < kWindow * 2; i++) {
            sum += static_cast<double>(stereo[at + i]) * stereo[at + i];
        }
        out.push_back(std::sqrt(sum / (kWindow * 2)));
    }
    return out;
}

double Correlation(const std::vector<double> &a, const std::vector<double> &b) {
    const std::size_t n = std::min(a.size(), b.size());
    double            ma = 0.0;
    double            mb = 0.0;
    for (std::size_t i = 0; i < n; i++) {
        ma += a[i];
        mb += b[i];
    }
    ma /= static_cast<double>(n);
    mb /= static_cast<double>(n);
    double ab = 0.0;
    double aa = 0.0;
    double bb = 0.0;
    for (std::size_t i = 0; i < n; i++) {
        ab += (a[i] - ma) * (b[i] - mb);
        aa += (a[i] - ma) * (a[i] - ma);
        bb += (b[i] - mb) * (b[i] - mb);
    }
    return aa > 0.0 && bb > 0.0 ? ab / std::sqrt(aa * bb) : (aa == bb ? 1.0 : 0.0);
}

double Rms(const std::vector<float> &a) {
    double sum = 0.0;
    for (const float x : a) {
        sum += static_cast<double>(x) * x;
    }
    return a.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(a.size()));
}

// DC_DATA is a data folder with published sound. Every music list (a bank and a sequence, in sound/bgm) played for eight seconds through
// Kazarov's mixer (reverb off) and Noktus's sequencer: their loudness envelopes (10 ms windows, when each note comes and goes) should match, and
// their loudness. Their waveforms part by design (the Gaussian interpolation's softer top, the pitch register's rounding over seconds).
TEST(NoktusSequenceRealData, PlayTheMusicAsKazarovDoes) {
    const char *env = std::getenv("DC_DATA");
    if (env == nullptr || *env == '\0') {
        GTEST_SKIP() << "DC_DATA is not set";
    }
    const fs::path        data = env;
    std::vector<fs::path> packs;
    if (fs::exists(data / "sound/bgm")) {
        for (const auto &entry : fs::directory_iterator(data / "sound/bgm")) {
            if (entry.path().filename().string().ends_with(".sound.json")) {
                packs.push_back(entry.path());
            }
        }
    }
    if (packs.empty()) {
        GTEST_SKIP() << "no published music in DC_DATA";
    }
    std::ranges::sort(packs);
    constexpr int       kFrames = 48000 * 8;
    std::vector<double> timings;
    std::vector<double> ratios;

    struct Worst {
        double      timing;
        std::string what;
    };

    std::vector<Worst> worst;
    const char        *out_dir = std::getenv("DC_NOKTUS_COMPARE_OUT");
    for (const fs::path &pack : packs) {
        const nlohmann::json document = nlohmann::json::parse(Text(pack), nullptr, false);
        if (document.is_discarded()) {
            continue;
        }
        for (const auto &list : document.value("lists", nlohmann::json::array())) {
            std::string bank_path;
            std::string sequence_path;
            for (const auto &load : list.value("load", nlohmann::json::array())) {
                if (load.contains("bank")) {
                    bank_path = load["bank"];
                } else if (load.contains("sequence")) {
                    sequence_path = load["sequence"];
                }
            }
            if (bank_path.empty() || sequence_path.empty()) {
                continue;
            }
            const std::string bank_text = Text(data / bank_path);
            const std::string sequence_text = Text(data / sequence_path);
            const auto        kazarov_bank = audio::Bank::CreateFromJson(bank_text, [&](const fs::path &path) { return data / path; });
            const auto        kazarov_song = audio::SqFile::CreateFromJson(sequence_text);
            const auto        noktus_bank = sound::NoktusBankFromJson(bank_text, [&](const std::string &path) { return ReadFile(data / path); });
            const auto        noktus_song = sound::NoktusSequenceFromJson(sequence_text);
            ASSERT_EQ(kazarov_song == nullptr, noktus_song == nullptr) << sequence_path;
            if (kazarov_bank == nullptr || noktus_bank == nullptr || noktus_song == nullptr) {
                continue;
            }

            audio::Mixer mixer(48000);
            mixer.SetReverbEnabled(false);
            mixer.BindBank(0, kazarov_bank);
            mixer.SetSequence(0, kazarov_song);
            mixer.SetVolume(0, audio::kFullPortVolume);
            mixer.Play(0);
            std::vector<float> kazarov(kFrames * 2);
            mixer.Render(kazarov.data(), kFrames);

            auto engine = noktus::audio::AudioEngine::Create({.sample_rate = 48000, .speakers = noktus::audio::Speakers::Stereo, .device = false});
            auto player = std::make_shared<noktus::audio::SequencePlayer>();
            player->SetBank(noktus_bank);
            player->SetSequence(noktus_song);
            player->Play();
            engine->Attach(player);
            std::vector<float> noktus(kFrames * 2);
            engine->Render(noktus);
            // Kazarov clips its output at full scale, as the PS2's saturated; Noktus's output stage doesn't yet (its plan, slice 7), and many of the
            // game's pieces at full volume peak past it, so they're compared as Kazarov gives them.
            for (float &x : noktus) {
                x = std::clamp(x, -1.0f, 1.0f);
            }

            const double kazarov_rms = Rms(kazarov);
            if (kazarov_rms < 1e-4 && Rms(noktus) < 1e-4) {
                continue;
            }
            const double timing = Correlation(Envelope(kazarov), Envelope(noktus));
            timings.push_back(timing);
            ratios.push_back(kazarov_rms > 0.0 ? Rms(noktus) / kazarov_rms : 0.0);
            const std::string what = pack.filename().string() + " " + fs::path(sequence_path).filename().string();
            worst.push_back({timing, what + " (loudness " + std::to_string(ratios.back()) + ")"});
            if (out_dir != nullptr) {
                noktus::audio::WavWriter a;
                noktus::audio::WavWriter b;
                const std::string        stem = (fs::path(out_dir) / fs::path(sequence_path).stem()).string();
                if (a.Open(stem + "_kazarov.wav", 48000, noktus::audio::Speakers::Stereo) && b.Open(stem + "_noktus.wav", 48000, noktus::audio::Speakers::Stereo)) {
                    a.Write(kazarov);
                    b.Write(noktus);
                }
                // For listening: the same with the list's own reverb (its .cfg's REVERBE, on core 0, the music's), Kazarov's beside the SPU2's.
                const auto found = list.find("config");
                const bool configured = found != list.end() && found->is_object();
                const auto number = [&](const char *key) {
                    const auto at = configured ? found->find(key) : list.end();
                    return configured && at != found->end() && at->is_number_integer() ? at->get<int>() : 0;
                };
                const int mode = number("reverb_mode");
                const int depth = number("reverb_depth");
                if (mode != 0) {
                    audio::Mixer wet_mixer(48000);
                    wet_mixer.SetReverbEnabled(true);
                    wet_mixer.SetReverb(0, mode, depth);
                    wet_mixer.BindBank(0, kazarov_bank);
                    wet_mixer.SetSequence(0, kazarov_song);
                    wet_mixer.SetVolume(0, audio::kFullPortVolume);
                    wet_mixer.Play(0);
                    std::vector<float> kazarov_wet(kFrames * 2);
                    wet_mixer.Render(kazarov_wet.data(), kFrames);
                    auto wet_engine = noktus::audio::AudioEngine::Create({.sample_rate = 48000, .speakers = noktus::audio::Speakers::Stereo, .device = false});
                    // The EVOL the game writes: the depth shifted up eight bits, a signed register (128 and more negative).
                    wet_engine->SetReverb(0, static_cast<noktus::audio::ReverbMode>(std::clamp(mode, 0, 9)), static_cast<float>(static_cast<std::int16_t>((depth << 8) & 0xFFFF)) / 32768.0f);
                    auto wet_player = std::make_shared<noktus::audio::SequencePlayer>();
                    wet_player->SetBank(noktus_bank);
                    wet_player->SetSequence(noktus_song);
                    wet_player->Play();
                    wet_engine->Attach(wet_player);
                    std::vector<float> noktus_wet(kFrames * 2);
                    wet_engine->Render(noktus_wet);
                    for (float &x : noktus_wet) {
                        x = std::clamp(x, -1.0f, 1.0f);
                    }
                    noktus::audio::WavWriter c;
                    noktus::audio::WavWriter d;
                    if (c.Open(stem + "_kazarov_reverb.wav", 48000, noktus::audio::Speakers::Stereo) &&
                        d.Open(stem + "_noktus_reverb.wav", 48000, noktus::audio::Speakers::Stereo)) {
                        c.Write(kazarov_wet);
                        d.Write(noktus_wet);
                    }
                }
            }
        }
    }
    ASSERT_FALSE(timings.empty());
    std::vector<double> sorted = timings;
    std::ranges::sort(sorted);
    std::vector<double> loud = ratios;
    std::ranges::sort(loud);
    const auto at = [](const std::vector<double> &v, double share) { return v[std::min(v.size() - 1, static_cast<std::size_t>(share * static_cast<double>(v.size())))]; };
    std::printf("%zu pieces: envelope correlation median %.4f, tenth %.4f, least %.4f\n", timings.size(), at(sorted, 0.5), at(sorted, 0.1), sorted.front());
    std::printf("loudness Noktus over Kazarov: median %.3f, tenth %.3f, ninetieth %.3f\n", at(loud, 0.5), at(loud, 0.1), at(loud, 0.9));
    std::ranges::sort(worst, {}, &Worst::timing);
    for (std::size_t i = 0; i < std::min<std::size_t>(worst.size(), 8); i++) {
        std::printf("  %.4f %s\n", worst[i].timing, worst[i].what.c_str());
    }
    EXPECT_GE(sorted.front(), 0.97);
    EXPECT_NEAR(at(loud, 0.5), 1.0, 0.03);
    EXPECT_GE(loud.front(), 0.9);
}

} // namespace
