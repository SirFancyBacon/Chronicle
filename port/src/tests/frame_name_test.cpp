#include <gtest/gtest.h>

#include "dataset.hpp"

// What frames' names say about their light: lights (self-lit and unfogged, not skies), and faces and eyes, lit with modern light with a floor.
namespace {

using dc::game::FrameNameIsFace;
using dc::game::FrameNameIsLight;

TEST(FrameName, FacesAndEyes) {
    EXPECT_TRUE(FrameNameIsFace("face__c"));
    EXPECT_TRUE(FrameNameIsFace("eye__cba"));
    EXPECT_TRUE(FrameNameIsFace("eye__cba2"));
    EXPECT_TRUE(FrameNameIsFace("EYE__CBY"));
    EXPECT_TRUE(FrameNameIsFace("l_eye__cba"));
    EXPECT_TRUE(FrameNameIsFace("r_eye__cba3"));
    EXPECT_FALSE(FrameNameIsFace("l_arm__c"));
    EXPECT_FALSE(FrameNameIsFace("meki1__czapp")); // a glow, not an eye
    EXPECT_FALSE(FrameNameIsFace("eyelid__c"));
    EXPECT_FALSE(FrameNameIsFace("face"));
    EXPECT_FALSE(FrameNameIsFace("sky__c"));
}

TEST(FrameName, Lights) {
    EXPECT_TRUE(FrameNameIsLight("win1__cf"));
    EXPECT_FALSE(FrameNameIsLight("face__c")); // fogged: not a light
    EXPECT_FALSE(FrameNameIsLight("sky__CFS"));
    EXPECT_FALSE(FrameNameIsLight("tenkyu__CFS"));
}

} // namespace
