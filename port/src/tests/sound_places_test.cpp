// Sounds heard from their places (Noktus's AUDIO_PLAN.md, slice 7 step 3): the listener taken from the game's camera, and a sound heard from
// several places split among voices.
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include "snd.hpp"
#include "sound/backend.hpp"
#include "sound/places.hpp"

namespace {

using namespace dc;
using Place = std::array<float, 3>;

float Dot(const Place &a, const Place &b) {
    return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]);
}

// Wherever the camera stands and looks, a sound the game pans to the right is on the listener's right, one ahead is ahead.
TEST(SoundPlaces, TheListenerAgreesWithTheGamesPan) {
    const std::vector<std::array<float, 4>> cameras = {
        {0,   0,  0,   0},
        {100, 20, -50, 0},
        {-30, 0,  400, 0}
    };
    const std::vector<std::array<float, 4>> looks = {
        {0,    0,     1,  0},
        {1,    0,     0,  0},
        {-1,   -0.5f, -1, 0},
        {0.3f, 0.2f,  -1, 0}
    };
    for (std::array<float, 4> camera : cameras) {
        for (std::array<float, 4> look : looks) {
            game::SndSetCamera(camera.data(), look.data());
            const std::optional<noktus::audio::Listener> listener = sound::ListenerFor(camera.data(), look.data());
            ASSERT_TRUE(listener.has_value());
            EXPECT_NEAR(Dot(listener->forward, listener->right), 0.0f, 1e-5f);
            for (int around = 0; around < 12; around++) {
                const float          angle = static_cast<float>(around) * 0.5236f + 0.2f; // never straight ahead or behind
                std::array<float, 4> source = {camera[0] + 80.0f * std::sin(angle), camera[1] + 5.0f, camera[2] + 80.0f * std::cos(angle), 0};
                float                volume = 0.0f;
                float                pan = 0.0f;
                game::SndGetVolPan(&volume, &pan, source.data(), 20.0f, 500.0f);
                const Place to = {source[0] - camera[0], 0.0f, source[2] - camera[2]};
                const float side = Dot(to, listener->right);
                if (std::abs(pan) > 1e-4f) {
                    EXPECT_EQ(pan > 0.0f, side > 0.0f) << "looking " << look[0] << "," << look[2] << " at angle " << angle;
                }
                const Place ahead = {look[0], 0.0f, look[2]};
                EXPECT_EQ(Dot(to, ahead) > 0.0f, Dot(to, listener->forward) > 0.0f);
            }
        }
    }
}

TEST(SoundPlaces, NoListenerLookingStraightUpOrDown) {
    const float camera[3] = {0, 0, 0};
    const float up[3] = {0, 1, 0};
    EXPECT_FALSE(sound::ListenerFor(camera, up).has_value());
}

TEST(SoundPlaces, OnePlaceIsRetailsOneVoice) {
    const std::array<float, 1> volumes = {0.6f};
    const std::array<Place, 1> places = {
        Place{10, 0, 0}
    };
    std::array<sound::SplitVoice, 3> out;
    ASSERT_EQ(sound::SplitAmong(volumes, places, {}, out), 1);
    EXPECT_EQ(out[0].place, 0);
    EXPECT_FLOAT_EQ(out[0].volume, 0.6f);
    const std::array<float, 1> loud = {1.3f};
    ASSERT_EQ(sound::SplitAmong(loud, places, {}, out), 1);
    EXPECT_FLOAT_EQ(out[0].volume, 1.0f); // summed volumes held at whole, as retail's
}

TEST(SoundPlaces, SplitVoicesAreAsLoudTogetherAsRetailsOne) {
    // Five places, three voices: the loudest three, their power that of retail's one voice (0.2 + 0.5 + 0.1 + 0.3 + 0.05, held at whole).
    const std::array<float, 5> volumes = {0.2f, 0.5f, 0.1f, 0.3f, 0.05f};
    const std::array<Place, 5> places = {
        Place{1, 0, 0},
        Place{2, 0, 0},
        Place{3, 0, 0},
        Place{4, 0, 0},
        Place{5, 0, 0}
    };
    std::array<sound::SplitVoice, 3> out;
    ASSERT_EQ(sound::SplitAmong(volumes, places, {}, out), 3);
    EXPECT_EQ(out[0].place, 1);
    EXPECT_EQ(out[1].place, 3);
    EXPECT_EQ(out[2].place, 0);
    float power = 0.0f;
    for (const sound::SplitVoice &voice : out) {
        power += voice.volume * voice.volume;
    }
    EXPECT_NEAR(std::sqrt(power), 1.0f, 1e-5f);
    // Their shares keep the places' proportions.
    EXPECT_NEAR(out[0].volume / out[1].volume, 0.5f / 0.3f, 1e-5f);

    // Quieter places: together the sum of all of theirs.
    const std::array<float, 2> quiet = {0.2f, 0.2f};
    ASSERT_EQ(sound::SplitAmong(quiet, std::span(places).first(2), {}, out), 2);
    EXPECT_NEAR(std::hypot(out[0].volume, out[1].volume), 0.4f, 1e-5f);

    const std::array<float, 2> silent = {0.0f, 0.0f};
    ASSERT_EQ(sound::SplitAmong(silent, std::span(places).first(2), {}, out), 2);
    EXPECT_EQ(out[0].volume, 0.0f);
    EXPECT_EQ(out[1].volume, 0.0f);
}

TEST(SoundPlaces, EachVoiceKeepsToItsPlace) {
    // Voice 0 was at the second place, voice 1 at the first; the first is now the louder. Each keeps to its own, and a new voice takes the
    // loudest left.
    const std::array<float, 3> volumes = {0.6f, 0.4f, 0.3f};
    const std::array<Place, 3> places = {
        Place{-50, 0, 0 },
        Place{50,  0, 0 },
        Place{0,   0, 90}
    };
    const std::array<Place, 2> was = {
        Place{49,  0, 1},
        Place{-51, 0, 0}
    };
    std::array<sound::SplitVoice, 3> out;
    ASSERT_EQ(sound::SplitAmong(volumes, places, was, out), 3);
    EXPECT_EQ(out[0].place, 1);
    EXPECT_EQ(out[1].place, 0);
    EXPECT_EQ(out[2].place, 2);
}

} // namespace
