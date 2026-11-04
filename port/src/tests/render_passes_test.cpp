#include <gtest/gtest.h>

#include <string_view>

#include "render/passes.hpp"

namespace dc::test {

using render::CurrentPass;
using render::Pass;
using render::PassName;
using render::PassScope;
using render::SetPass;

TEST(RenderPasses, EveryPassHasAName) {
    for (int pass = 0; pass < static_cast<int>(Pass::Count); pass++) {
        EXPECT_FALSE(std::string_view(PassName(static_cast<Pass>(pass))).empty());
    }
    EXPECT_STREQ(PassName(Pass::Unnamed), "Unnamed");
    EXPECT_STREQ(PassName(Pass::Screen), "Screen");
}

TEST(RenderPasses, AScopeReturnsToThePassItInterrupted) {
    SetPass(Pass::Effects);
    {
        PassScope flare(Pass::LensFlare);
        EXPECT_EQ(CurrentPass(), Pass::LensFlare);
        {
            PassScope nested(Pass::Fade);
            EXPECT_EQ(CurrentPass(), Pass::Fade);
        }
        EXPECT_EQ(CurrentPass(), Pass::LensFlare);
    }
    EXPECT_EQ(CurrentPass(), Pass::Effects);
    SetPass(Pass::Unnamed);
}

} // namespace dc::test
