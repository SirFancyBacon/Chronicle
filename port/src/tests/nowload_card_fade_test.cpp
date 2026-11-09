#include <gtest/gtest.h>

#include "nowload.hpp"

namespace dc::test {

using dc::game::AreaCardAlpha;

// The area card eases in over 0.6 s, holds for 1.4 and eases out over 0.6: clear at both ends, half way through each
// fade at half, never brighter than full, and moving smoothly, slowest at the fades' ends.
TEST(NowLoadCardFade, EasesInHoldsAndEasesOut) {
    EXPECT_EQ(AreaCardAlpha(0.0), 0);
    EXPECT_EQ(AreaCardAlpha(0.3), 0x40);
    EXPECT_EQ(AreaCardAlpha(0.6), 0x80);
    EXPECT_EQ(AreaCardAlpha(1.3), 0x80);
    EXPECT_EQ(AreaCardAlpha(2.0), 0x80);
    EXPECT_EQ(AreaCardAlpha(2.3), 0x40);
    EXPECT_EQ(AreaCardAlpha(2.6), 0);
    EXPECT_EQ(AreaCardAlpha(5.0), 0);
    EXPECT_LE(AreaCardAlpha(0.05), 3);  // eased: 2% a twelfth of the way in, not the 8% a straight fade would be
    int last = 0;
    for (int ms = 0; ms <= 600; ms += 5) {
        const int alpha = AreaCardAlpha(ms / 1000.0);
        EXPECT_GE(alpha, last);
        EXPECT_LE(alpha - last, 2) << ms;
        last = alpha;
    }
}

} // namespace dc::test
