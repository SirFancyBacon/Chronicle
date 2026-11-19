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
#include "noktus/audio.hpp"
#include "noktus/synth.hpp"
#include "sound/published.hpp"

// Noktus's voices (its audio plan's slice 2) against Kazarov's, which Chronicle plays today: the bank documents read for Noktus, and the same keys
// played through both.
namespace {

namespace fs = std::filesystem;
using namespace dc;

std::vector<std::uint8_t> Wav16(int rate, const std::vector<std::int16_t> &values) {
    std::vector<std::uint8_t> out;
    const auto                put32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; i++) {
            out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    };
    const auto put16 = [&](std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const auto tag = [&](const char *t) { out.insert(out.end(), t, t + 4); };
    tag("RIFF");
    put32(36 + static_cast<std::uint32_t>(values.size() * 2));
    tag("WAVE");
    tag("fmt ");
    put32(16);
    put16(1);
    put16(1);
    put32(static_cast<std::uint32_t>(rate));
    put32(static_cast<std::uint32_t>(rate * 2));
    put16(2);
    put16(16);
    tag("data");
    put32(static_cast<std::uint32_t>(values.size() * 2));
    for (const std::int16_t v : values) {
        put16(static_cast<std::uint16_t>(v));
    }
    return out;
}

TEST(NoktusBank, ReadsABankDocumentAndItsWaves) {
    const std::string               text = R"({"kind": "bank", "schema_version": 1,
        "programs": [{"present": true, "transpose": 1, "detune": 2, "volume": 100, "pan": 60,
                      "splits": [{"key_low": 10, "key_high": 20, "sample_set": 0, "transpose": 3, "detune": 4, "volume": 90, "pan": 70,
                                  "bend_low": 128, "bend_high": 256}]},
                     {"present": false}],
        "sample_sets": [{"samples": [0], "vel_curve": 2, "vel_low": 5, "vel_high": 120}],
        "samples": [{"vag": 0, "base_note": 62, "detune": -3, "volume": 110, "pan": 50, "adsr1": 33023, "adsr2": 57321, "spu_attr": 47,
                     "vel_low": 1, "vel_high": 127}],
        "vags": [{"wav": "sound/banks/x/samples/0.wav", "loop_start": 2, "loop_end": 4, "loops": true, "attr": 0}]})";
    const std::vector<std::uint8_t> wav = Wav16(22050, {0, 16384, -16384, 32767, 5});
    std::string                     asked;
    const auto                      bank = sound::NoktusBankFromJson(text, [&](const std::string &path) -> std::optional<std::vector<std::uint8_t>> {
        asked = path;
        return wav;
    });
    ASSERT_NE(bank, nullptr);
    EXPECT_EQ(asked, "sound/banks/x/samples/0.wav");
    ASSERT_EQ(bank->programs.size(), 2u);
    EXPECT_TRUE(bank->programs[0].present);
    EXPECT_FALSE(bank->programs[1].present);
    const auto &split = bank->programs[0].splits.at(0);
    EXPECT_EQ(split.key_low, 10);
    EXPECT_EQ(split.bend_high, 256);
    EXPECT_EQ(bank->sample_sets.at(0).velocity_curve, 2);
    const auto &sample = bank->samples.at(0);
    EXPECT_EQ(sample.base_note, 62);
    EXPECT_EQ(sample.adsr1, 33023);
    EXPECT_EQ(sample.spu_attribute, 47);
    const auto &wave = bank->waves.at(0);
    ASSERT_NE(wave.samples, nullptr);
    EXPECT_EQ(wave.samples->rate, 22050);
    ASSERT_EQ(wave.samples->data.size(), 4u); // cut where its loop ends
    EXPECT_FLOAT_EQ(wave.samples->data[1], 0.5f);
    EXPECT_EQ(wave.loop_start, 2u);
    EXPECT_TRUE(wave.loops);
    // A document whose keys are of the wrong kind (a datapack's, say) is no bank, not a throw.
    EXPECT_EQ(sound::NoktusBankFromJson(R"({"kind": "bank", "programs": [{"present": 1}, 5], "vags": []})",
                                        [&](const std::string &) { return std::optional<std::vector<std::uint8_t>>(wav); }),
              nullptr);
    EXPECT_EQ(sound::NoktusBankFromJson(R"({"kind": 3})", [&](const std::string &) { return std::optional<std::vector<std::uint8_t>>(wav); }), nullptr);
    EXPECT_EQ(sound::NoktusBankFromJson(R"([1, 2])", [&](const std::string &) { return std::optional<std::vector<std::uint8_t>>(wav); }), nullptr);
    // A wave outside the data is refused.
    EXPECT_EQ(sound::NoktusBankFromJson(R"({"kind": "bank", "vags": [{"wav": "../x.wav", "loop_start": 0, "loop_end": 1}]})",
                                        [&](const std::string &) { return std::optional<std::vector<std::uint8_t>>(wav); }),
              nullptr);
}

std::optional<std::vector<std::uint8_t>> ReadFile(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {});
}

// The best normalised correlation of a and b (interleaved stereo) within a few frames' shift either way (Gaussian interpolation lags a frame or two
// behind Kazarov's Catmull-Rom), each side against its own.
double Correlation(const std::vector<float> &a, const std::vector<float> &b) {
    const std::ptrdiff_t frames = static_cast<std::ptrdiff_t>(std::min(a.size(), b.size()) / 2);
    double               best = -1.0;
    for (std::ptrdiff_t lag = -6; lag <= 6; lag++) {
        double ab = 0.0;
        double aa = 0.0;
        double bb = 0.0;
        for (std::ptrdiff_t i = 6; i + 6 < frames; i++) {
            for (std::ptrdiff_t side = 0; side < 2; side++) {
                const double x = a[static_cast<std::size_t>(i * 2 + side)];
                const double y = b[static_cast<std::size_t>((i + lag) * 2 + side)];
                ab += x * y;
                aa += x * x;
                bb += y * y;
            }
        }
        if (aa > 0.0 && bb > 0.0) {
            best = std::max(best, ab / std::sqrt(aa * bb));
        }
    }
    return best;
}

double Rms(const std::vector<float> &a) {
    double sum = 0.0;
    for (const float x : a) {
        sum += static_cast<double>(x) * x;
    }
    return a.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(a.size()));
}

// DC_DATA is a data folder with published sound. Every bank's present programs (up to three each), a key in the middle of their first split at
// velocity 100, held a quarter second and let go for another, through Kazarov's mixer (reverb off) and Noktus's synth. They differ by design in
// the interpolation (the SPU2's Gaussian, Kazarov's Catmull-Rom) and in the envelope's exact steps; the shape and loudness should agree.
TEST(NoktusVoicesRealData, PlayTheBanksAsKazarovDoes) {
    const char *env = std::getenv("DC_DATA");
    if (env == nullptr || *env == '\0') {
        GTEST_SKIP() << "DC_DATA is not set";
    }
    const fs::path        data = env;
    std::vector<fs::path> documents;
    if (fs::exists(data / "sound/banks")) {
        for (const auto &entry : fs::recursive_directory_iterator(data / "sound/banks")) {
            if (entry.is_regular_file() && entry.path().filename() == "bank.json") {
                documents.push_back(entry.path());
            }
        }
    }
    if (documents.empty()) {
        GTEST_SKIP() << "no published banks in DC_DATA";
    }
    std::ranges::sort(documents);
    constexpr int       kHeld = 12000;
    constexpr int       kVelocity = 100;
    std::vector<double> correlations;
    std::vector<double> ratios;

    struct Worst {
        double      correlation;
        std::string what;
    };

    std::vector<Worst> worst;
    const char        *out_dir = std::getenv("DC_NOKTUS_COMPARE_OUT");
    for (const fs::path &document : documents) {
        const auto text = ReadFile(document);
        ASSERT_TRUE(text.has_value());
        const std::string json(text->begin(), text->end());
        const auto        kazarov_bank = audio::Bank::CreateFromJson(json, [&](const fs::path &path) { return data / path; });
        const auto        noktus_bank = sound::NoktusBankFromJson(json, [&](const std::string &path) { return ReadFile(data / path); });
        ASSERT_EQ(kazarov_bank == nullptr, noktus_bank == nullptr) << document;
        if (noktus_bank == nullptr) {
            continue;
        }
        int played = 0;
        for (int program = 0; program < static_cast<int>(noktus_bank->programs.size()) && program < 128 && played < 3; program++) {
            const auto &p = noktus_bank->programs[program];
            if (!p.present || p.splits.empty()) {
                continue;
            }
            const int note = std::clamp((p.splits[0].key_low + p.splits[0].key_high) / 2, 0, 127);
            played++;

            audio::Mixer mixer(48000);
            mixer.SetReverbEnabled(false);
            mixer.BindBank(0, kazarov_bank);
            mixer.SetVolume(0, audio::kFullPortVolume);
            mixer.ShortMessage(0, 0xC0u | static_cast<std::uint32_t>(program) << 8);
            mixer.ShortMessage(0, 0x90u | static_cast<std::uint32_t>(note) << 8 | static_cast<std::uint32_t>(kVelocity) << 16);
            std::vector<float> kazarov(kHeld * 2 * 2);
            mixer.Render(kazarov.data(), kHeld);
            mixer.ShortMessage(0, 0x80u | static_cast<std::uint32_t>(note) << 8);
            mixer.Render(kazarov.data() + kHeld * 2, kHeld);

            auto                 engine = noktus::audio::AudioEngine::Create({.sample_rate = 48000, .speakers = noktus::audio::Speakers::Stereo, .device = false});
            noktus::audio::Synth synth(*engine);
            // Kazarov's channel starts at volume 100 and expression 127.
            const auto         key = synth.KeyOn(noktus_bank, {.program = program, .note = note, .velocity = kVelocity, .volume = 100.0f / 128.0f * 127.0f / 128.0f});
            std::vector<float> noktus(kHeld * 2 * 2);
            engine->Render(std::span(noktus).first(kHeld * 2));
            synth.KeyOff(key);
            engine->Render(std::span(noktus).subspan(kHeld * 2));

            const double      kazarov_rms = Rms(kazarov);
            const double      noktus_rms = Rms(noktus);
            const std::string what = document.parent_path().filename().string() + " program " + std::to_string(program) + " note " + std::to_string(note);
            if (kazarov_rms < 1e-4 && noktus_rms < 1e-4) {
                continue; // silent in both
            }
            const double correlation = Correlation(kazarov, noktus);
            correlations.push_back(correlation);
            ratios.push_back(kazarov_rms > 0.0 ? noktus_rms / kazarov_rms : 0.0);
            worst.push_back({correlation, what + " (loudness " + std::to_string(ratios.back()) + ")"});
            if (out_dir != nullptr && correlation < 0.9) {
                noktus::audio::WavWriter a;
                noktus::audio::WavWriter b;
                const std::string        stem = (fs::path(out_dir) / (document.parent_path().filename().string() + "_" + std::to_string(program))).string();
                if (a.Open(stem + "_kazarov.wav", 48000, noktus::audio::Speakers::Stereo) && b.Open(stem + "_noktus.wav", 48000, noktus::audio::Speakers::Stereo)) {
                    a.Write(kazarov);
                    b.Write(noktus);
                }
            }
        }
    }
    ASSERT_FALSE(correlations.empty());
    std::vector<double> sorted = correlations;
    std::ranges::sort(sorted);
    std::vector<double> loud = ratios;
    std::ranges::sort(loud);
    const auto at = [](const std::vector<double> &v, double share) { return v[std::min(v.size() - 1, static_cast<std::size_t>(share * static_cast<double>(v.size())))]; };
    const auto above = std::ranges::count_if(correlations, [](double c) { return c >= 0.9; });
    std::printf("%zu keys: correlation median %.4f, tenth %.4f, least %.4f; %ld of them 0.9 or more\n", correlations.size(), at(sorted, 0.5), at(sorted, 0.1),
                sorted.front(), static_cast<long>(above));
    std::printf("loudness Noktus over Kazarov: median %.3f, tenth %.3f, ninetieth %.3f\n", at(loud, 0.5), at(loud, 0.1), at(loud, 0.9));
    std::ranges::sort(worst, {}, &Worst::correlation);
    for (std::size_t i = 0; i < std::min<std::size_t>(worst.size(), 12); i++) {
        std::printf("  %.4f %s\n", worst[i].correlation, worst[i].what.c_str());
    }
    EXPECT_GE(at(sorted, 0.5), 0.95);
    EXPECT_NEAR(at(loud, 0.5), 1.0, 0.1);
}

} // namespace
