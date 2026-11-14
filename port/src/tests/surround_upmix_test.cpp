#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

#include "platform/audio_output.hpp"

// Surround's spread of the stereo mix: the bass channel stays silent, whatever the mix holds, so a receiver's bass management alone feeds the subwoofer;
// the fronts and centre carry the mix as before.
namespace {

constexpr int   kRate = 48000;
constexpr float kPi = 3.14159265f;

// A second of stereo: a deep tone, the same in both channels, under a crackle-like tone a little louder on the left.
std::vector<float> Mix() {
    std::vector<float> stereo(static_cast<std::size_t>(kRate) * 2);
    for (int i = 0; i < kRate; i++) {
        const float t = static_cast<float>(i) / kRate;
        const float bass = 0.5f * std::sin(2.0f * kPi * 50.0f * t);
        const float crackle = 0.3f * std::sin(2.0f * kPi * 900.0f * t);
        stereo[static_cast<std::size_t>(i) * 2] = bass + crackle;
        stereo[static_cast<std::size_t>(i) * 2 + 1] = bass + 0.5f * crackle;
    }
    return stereo;
}

TEST(SurroundUpmix, BassChannelStaysSilent) {
    const std::vector<float>    stereo = Mix();
    std::vector<float>          wide(static_cast<std::size_t>(kRate) * 6);
    dc::platform::SurroundUpmix upmix;
    upmix.Reset(kRate);
    upmix.Run(stereo.data(), wide.data(), kRate, kRate);
    for (int i = 0; i < kRate; i++) {
        ASSERT_EQ(wide[static_cast<std::size_t>(i) * 6 + 3], 0.0f) << "frame " << i;
    }
}

TEST(SurroundUpmix, FrontsAndCentreCarryTheMix) {
    const std::vector<float>    stereo = Mix();
    std::vector<float>          wide(static_cast<std::size_t>(kRate) * 6);
    dc::platform::SurroundUpmix upmix;
    upmix.Reset(kRate);
    upmix.Run(stereo.data(), wide.data(), kRate, kRate);
    for (int i = 0; i < kRate; i += 997) {
        const float  l = stereo[static_cast<std::size_t>(i) * 2];
        const float  r = stereo[static_cast<std::size_t>(i) * 2 + 1];
        const float *o = &wide[static_cast<std::size_t>(i) * 6];
        EXPECT_FLOAT_EQ(o[0], l * 0.85f);
        EXPECT_FLOAT_EQ(o[1], r * 0.85f);
        EXPECT_FLOAT_EQ(o[2], (l + r) * 0.5f * 0.7f);
        EXPECT_FLOAT_EQ(o[4], -o[5]); // the rears: the side, opposite ways
    }
}

} // namespace
