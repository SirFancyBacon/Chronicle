#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "noktus/audio.hpp"
#include "noktus/synth.hpp"
#include "sound/published.hpp"

// Bank documents read for Noktus's voices (its audio plan's slice 2).
namespace {

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

} // namespace
