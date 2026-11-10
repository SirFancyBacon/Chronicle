#include <gtest/gtest.h>

#include "nowload.hpp"

namespace dc::test {

using dc::game::AreaCardAlpha;

// The area card fades for retail's ticks: eased in over 128, held for 120 and eased out over 128. Clear at both ends,
// half way through each fade at half, never brighter than full, and moving smoothly both ways, slowest at the fades'
// ends. (How long the loading screen then stays up is the tick loop's, which no test reaches.)
TEST(NowLoadCardFade, EasesInHoldsAndEasesOutForRetailsTicks) {
    constexpr double in = 128.0;
    constexpr double held = in + 120.0;
    constexpr double out = held + 128.0;
    EXPECT_EQ(AreaCardAlpha(0.0), 0);
    EXPECT_EQ(AreaCardAlpha(in / 2), 0x40);
    EXPECT_EQ(AreaCardAlpha(in), 0x80);
    EXPECT_EQ(AreaCardAlpha(180.0), 0x80);
    EXPECT_EQ(AreaCardAlpha(held - 0.5), 0x80);
    EXPECT_EQ(AreaCardAlpha((held + out) / 2), 0x40);
    EXPECT_EQ(AreaCardAlpha(out), 0);
    EXPECT_EQ(AreaCardAlpha(600.0), 0);
    EXPECT_LE(AreaCardAlpha(in / 12), 3); // eased: 2% a twelfth of the way in, not the 8% a straight fade would be
    EXPECT_LE(AreaCardAlpha(out - 128.0 / 12), 3);
    int last = 0;
    for (double t = 0.0; t <= in; t += 0.25) {
        const int alpha = AreaCardAlpha(t);
        EXPECT_GE(alpha, last) << t;
        EXPECT_LE(alpha - last, 1) << t;
        last = alpha;
    }
    for (double t = held; t <= out; t += 0.25) {
        const int alpha = AreaCardAlpha(t);
        EXPECT_LE(alpha, last) << t;
        EXPECT_LE(last - alpha, 1) << t;
        last = alpha;
    }
    EXPECT_EQ(last, 0);
}

} // namespace dc::test
