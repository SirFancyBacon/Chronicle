// The custom soundtrack's recordings decoded for Noktus (sound/soundtrack.hpp), from files made here.
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "platform/paths.hpp"
#include "platform/settings.hpp"
#include "sound/soundtrack.hpp"

namespace {

// A WAV of 16-bit samples, interleaved over its channels.
std::vector<std::uint8_t> Wav(int rate, int channels, const std::vector<std::int16_t> &values) {
    std::vector<std::uint8_t> out;
    const auto                put = [&](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; i++) {
            out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }
    };
    const auto data = static_cast<std::uint32_t>(values.size() * 2);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    put(36 + data, 4);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put(16, 4);
    put(1, 2);
    put(static_cast<std::uint32_t>(channels), 2);
    put(static_cast<std::uint32_t>(rate), 4);
    put(static_cast<std::uint32_t>(rate * channels * 2), 4);
    put(static_cast<std::uint32_t>(channels * 2), 2);
    put(16, 2);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    put(data, 4);
    for (const std::int16_t value : values) {
        put(static_cast<std::uint16_t>(value), 2);
    }
    return out;
}

} // namespace

TEST(Soundtrack, OneChannelIsHeardOnBothSides) {
    std::string error;
    const auto  samples = dc::sound::DecodeRecording(Wav(44100, 1, {16384, -8192}), ".WAV", error);
    ASSERT_NE(samples, nullptr) << error;
    EXPECT_EQ(samples->rate, 44100);
    EXPECT_EQ(samples->channels, 2);
    EXPECT_EQ(samples->data, (std::vector<float>{0.5f, 0.5f, -0.25f, -0.25f}));
}

TEST(Soundtrack, MoreThanTwoChannelsKeepTheFrontPair) {
    std::string error;
    const auto  samples = dc::sound::DecodeRecording(Wav(48000, 3, {16384, -16384, 8192}), ".wav", error);
    ASSERT_NE(samples, nullptr) << error;
    EXPECT_EQ(samples->data, (std::vector<float>{0.5f, -0.5f}));
}

TEST(Soundtrack, WhatCannotBeDecodedSaysWhy) {
    std::string error;
    EXPECT_EQ(dc::sound::DecodeRecording(std::vector<std::uint8_t>(16, 0), ".mp3", error), nullptr);
    EXPECT_NE(error.find("unsupported"), std::string::npos);
    EXPECT_EQ(dc::sound::DecodeRecording(std::vector<std::uint8_t>(16, 0), ".ogg", error), nullptr);
    EXPECT_TRUE(error.starts_with("cannot decode the file")) << error;
}

// soundtrack.json beside the saves names a recording for a sequence; asked for, it loads on the worker and comes back with its entry's settings.
TEST(Soundtrack, ARecordingLoadsFromItsMappingOnAWorker) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "chronicle_soundtrack_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "soundtrack");
    PathsSetSaveRoot(dir / "save");
    ASSERT_TRUE(dc::sound::SoundtrackEnsure()); // there's one already: left as it is
    {
        std::ofstream mapping(dir / "soundtrack" / "soundtrack.json");
        mapping << R"({"version": 1, "overrides": {"bg_001.sq": {"file": "tone.wav", "gain": 0.5, "loop_start": 0.25, "loop_end": 0.75},
                                                   "bg_002.sq": {"file": "missing.wav"}}})";
    }
    {
        const std::vector<std::uint8_t> wav = Wav(1000, 1, std::vector<std::int16_t>(1000, 8192));
        std::ofstream                   file(dir / "soundtrack" / "tone.wav", std::ios::binary);
        file.write(reinterpret_cast<const char *>(wav.data()), static_cast<std::streamsize>(wav.size()));
    }
    HostSettings settings = SettingsGet();
    settings.soundtrack = false;
    SettingsChange(settings);
    EXPECT_EQ(dc::sound::RecordingFor("bg_001.sq"), nullptr) << "the option's PS2";
    settings.soundtrack = true;
    SettingsChange(settings);

    std::shared_ptr<const dc::sound::Recording> recording;
    for (int wait = 0; wait < 500 && recording == nullptr; wait++) {
        recording = dc::sound::RecordingFor("bg_001.sq");
        if (recording == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    ASSERT_NE(recording, nullptr);
    ASSERT_NE(recording->samples, nullptr);
    EXPECT_EQ(recording->samples->channels, 2);
    EXPECT_EQ(recording->samples->Frames(), 1000u);
    EXPECT_EQ(recording->loop_start, 250u);
    EXPECT_EQ(recording->loop_end, 750u);
    EXPECT_TRUE(recording->loop);
    EXPECT_FLOAT_EQ(recording->gain, 0.5f);

    for (int wait = 0; wait < 50; wait++) {
        EXPECT_EQ(dc::sound::RecordingFor("bg_002.sq"), nullptr) << "its file isn't there";
        EXPECT_EQ(dc::sound::RecordingFor("bg_003.sq"), nullptr) << "no entry";
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    settings.soundtrack = false;
    SettingsChange(settings);
    fs::remove_all(dir);
}
