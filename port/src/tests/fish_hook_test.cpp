#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>

#include "fish.hpp"
#include "fishing/host.hpp"
#include "item/host.hpp"

namespace dc::test {

using namespace dc::game;

TEST(FishHook, AFishHoldsTheHookHalfASecondToASecond) {
    item::Load();
    fishing::Load();
    int shortest = 1000;
    int longest = 0;
    for (unsigned int seed = 1; seed <= 400; seed++) {
        std::srand(seed);
        CFish fish;
        fish.fish_kind = 9;
        fish.info = *fishing::Info(9);
        float food[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        fish.SetFoodPos(8, food, 50.0f); // the Carrot, which kind 9 likes most
        fish.action = FISH_ACTION_EAT_FOOD;
        fish.action_timer = -1;
        // Step until the fish takes the hook, then count the ticks it holds it.
        int tick = 0;
        while (fish.action == FISH_ACTION_EAT_FOOD && tick++ < 10000) {
            fish.action_timer = std::min(fish.action_timer, 0) - 1;
            fish.Step();
        }
        if (fish.action != FISH_ACTION_BITE_HOOK) {
            continue;
        }
        int held = 0;
        while (fish.action == FISH_ACTION_BITE_HOOK) {
            fish.Step();
            held++;
        }
        shortest = std::min(shortest, held);
        longest = std::max(longest, held);
    }
    EXPECT_EQ(shortest, kHookHoldFirst);
    EXPECT_EQ(longest, kHookHoldLast);
}

} // namespace dc::test
