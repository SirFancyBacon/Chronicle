#include "platform/speaker_test.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <random>
#include <string_view>
#include <vector>

#include "noktus/audio.hpp"

namespace dc::platform {

namespace {

namespace audio = noktus::audio;

constexpr int   kRate = 48000;
constexpr float kLevel = 0.25f; // -12 dB, as a receiver's own test tones sit under full scale

// Pink noise (Paul Kellet's filter of white noise), the test signal receivers use, even across the speakers' range. Eight seconds of it, the filter
// warmed up first, and its end crossfaded into its start, so it loops without a seam a listener could take for a fault.
std::shared_ptr<audio::Samples> PinkNoise(int channels, std::uint32_t seed) {
    constexpr int kLength = 8 * kRate;
    constexpr int kSeam = kRate / 2;
    constexpr int kWarm = kRate;
    auto          samples = std::make_shared<audio::Samples>();
    samples->rate = kRate;
    samples->channels = channels;
    samples->data.assign(static_cast<std::size_t>(channels) * kLength, 0.0f);
    std::mt19937                          random(seed);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    std::vector<float>                    run(static_cast<std::size_t>(kLength + kSeam));
    for (int c = 0; c < channels; c++) {
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        for (int i = -kWarm; i < kLength + kSeam; i++) {
            const float w = white(random);
            b0 = 0.99886f * b0 + w * 0.0555179f;
            b1 = 0.99332f * b1 + w * 0.0750759f;
            b2 = 0.96900f * b2 + w * 0.1538520f;
            b3 = 0.86650f * b3 + w * 0.3104856f;
            b4 = 0.55000f * b4 + w * 0.5329522f;
            b5 = -0.7616f * b5 - w * 0.0168980f;
            const float pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f) * 0.11f;
            b6 = w * 0.115926f;
            if (i >= 0) {
                run[static_cast<std::size_t>(i)] = pink;
            }
        }
        // The loop's last sample runs on into what followed it, faded into its first: at equal power, as the two are unrelated.
        for (int i = 0; i < kLength; i++) {
            float value = run[static_cast<std::size_t>(i)];
            if (i < kSeam) {
                const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(kSeam) * std::numbers::pi_v<float> * 0.5f;
                value = value * std::sin(t) + run[static_cast<std::size_t>(kLength + i)] * std::cos(t);
            }
            samples->data[static_cast<std::size_t>(i * channels + c)] = value;
        }
    }
    return samples;
}

float Radians(float degrees) { return degrees * std::numbers::pi_v<float> / 180.0f; }

void Say(const char *what) {
    std::printf("speaker test: %s\n", what);
    std::fflush(stdout);
}

} // namespace

SpeakerTest RunSpeakerTest(const char *layout) {
    const std::string_view name = layout;
    audio::Speakers        speakers;
    if (name == "stereo") {
        speakers = audio::Speakers::Stereo;
    } else if (name == "5.1") {
        speakers = audio::Speakers::Surround51;
    } else if (name == "7.1") {
        speakers = audio::Speakers::Surround71;
    } else {
        std::fprintf(stderr, "DC_SPEAKER_TEST: stereo, 5.1 or 7.1, not %s\n", layout);
        return SpeakerTest::BadLayout;
    }
    auto engine = audio::AudioEngine::Create({.sample_rate = kRate, .speakers = speakers});
    if (!engine->HasDevice()) {
        std::fprintf(stderr, "DC_SPEAKER_TEST: no audio device opened: %s\n", SDL_GetError());
        return SpeakerTest::NoDevice;
    }
    const auto noise = PinkNoise(1, 1);

    // Each speaker in turn, clockwise from the front left.
    struct Stop {
        const char *name;
        float       degrees;
    };

    std::vector<Stop> stops = {
        {"front left",  -30.0f},
        {"centre",      0.0f  },
        {"front right", 30.0f }
    };
    if (speakers == audio::Speakers::Surround51) {
        stops.insert(stops.end(), {
                                      {"surround right", 110.0f },
                                      {"surround left",  -110.0f}
        });
    } else if (speakers == audio::Speakers::Surround71) {
        stops.insert(stops.end(), {
                                      {"side right", 90.0f  },
                                      {"back right", 150.0f },
                                      {"back left",  -150.0f},
                                      {"side left",  -90.0f }
        });
    }
    if (speakers == audio::Speakers::Stereo) {
        stops = {
            {"left",          -90.0f},
            {"centre (both)", 0.0f  },
            {"right",         90.0f }
        };
    }
    for (const Stop &stop : stops) {
        Say(stop.name);
        const audio::SoundId sound = engine->Start(noise, {
                                                              .placement = {.kind = audio::Placement::Kind::Direction, .azimuth = Radians(stop.degrees)},
                                                              .volume = kLevel,
                                                              .loop = true
        });
        SDL_Delay(2000);
        engine->Stop(sound);
        SDL_Delay(500);
    }

    // A sound circling the room, twice, clockwise.
    Say("circling the room, clockwise, twice");
    const audio::SoundId circling = engine->Start(noise, {.placement = {.kind = audio::Placement::Kind::Direction}, .volume = kLevel, .loop = true});
    constexpr int        kSteps = 800; // 8 seconds
    for (int step = 0; step <= kSteps; step++) {
        const float turn = 2.0f * 2.0f * std::numbers::pi_v<float> * static_cast<float>(step) / static_cast<float>(kSteps);
        engine->Place(circling, {.kind = audio::Placement::Kind::Direction, .azimuth = turn});
        SDL_Delay(10);
    }
    engine->Stop(circling);
    SDL_Delay(500);

    // Music's spread: its left, then its right, all around (the surrounds under the front).
    const auto pair_noise = PinkNoise(2, 2);
    for (const bool left : {true, false}) {
        Say(left ? "music's left, spread along the left" : "music's right, spread along the right");
        auto one_side = std::make_shared<audio::Samples>(*pair_noise);
        for (std::size_t i = left ? 1 : 0; i < one_side->data.size(); i += 2) {
            one_side->data[i] = 0.0f;
        }
        const audio::SoundId music = engine->Start(one_side, {.bus = audio::Bus::Music, .placement = {.kind = audio::Placement::Kind::Around}, .volume = kLevel, .loop = true});
        SDL_Delay(3000);
        engine->Stop(music);
        SDL_Delay(500);
    }
    Say("done");
    return SpeakerTest::Done;
}

} // namespace dc::platform
