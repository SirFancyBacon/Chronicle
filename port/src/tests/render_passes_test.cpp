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

TEST(RenderPasses, APassGivesItsUsualLayerAndAScopeOverridesIt) {
    SetPass(Pass::Menu);
    EXPECT_EQ(render::CurrentLayer2D(), render::Layer2D::Ui);
    {
        render::Layer2DScope cover(render::Layer2D::Cover);
        EXPECT_EQ(render::CurrentLayer2D(), render::Layer2D::Cover);
        render::PassScope effects(Pass::Effects);
        EXPECT_EQ(render::CurrentLayer2D(), render::Layer2D::Cover);
    }
    EXPECT_EQ(render::CurrentLayer2D(), render::Layer2D::Ui);
    SetPass(Pass::Fade);
    EXPECT_EQ(render::CurrentLayer2D(), render::Layer2D::Window);
    SetPass(Pass::Unnamed);
}

} // namespace dc::test
