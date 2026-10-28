// nlohmann/json (through the framework) before the game headers, whose one-letter macros it cannot stand.
#include "fishing/session.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "fishing/framework.hpp"
#include "fishing/host.hpp"
#include "item/host.hpp"

namespace dc::test {

namespace {

using fishing::Step;

// Records what the session asks of the game, and answers as a test says.
class FakeAngler final : public fishing::Angler {
public:
    std::vector<std::string> calls;
    bool                     motion_done = false;
    int                      motion = 0;
    bool                     model_ready = false;
    bool                     message_done = false;
    std::deque<double>       rolls; // Random()'s answers, then 0.5
    int                      points = 0;
    std::vector<int>         records;
    int                      mardan_garayan = 0;
    int                      shown = 0;
    const fishing::Place    *place = fishing::PlaceAtSpot(2);
    std::optional<fishing::Catch> caught = fishing::Catch{.species = fishing::SpeciesOfKind(5), .size = 142, .points = 300};

    void Motion(int number, int flags) override {
        motion = number;
        Record(std::format("motion {} {}", number, flags));
    }
    int  CurrentMotion() override { return motion; }
    bool MotionDone() override { return motion_done; }
    void HoldStill() override {}
    void Turn(float) override {}
    void Play(fishing::Sound sound) override { Record(std::format("sound {}", static_cast<int>(sound))); }
    void Rumble(int intensity, int ticks) override { Record(std::format("rumble {} {}", intensity, ticks)); }
    void Ripple(float) override {}
    void PullFloat(float) override {}
    void BiteCue() override { Record("bite cue"); }
    void Help(bool) override {}
    void OpenMenu(int event) override { Record(std::format("menu {}", event)); }
    void FollowLine() override {}
    void CatchCamera(float) override {}
    void FadeMusic(bool out) override { Record(out ? "music out" : "music in"); }
    bool FloatCaught() override { return false; }
    bool HookLow() override { return true; }
    void LoseBait() override { Record("bait lost"); }
    void UseBait() override { Record("bait used"); }
    void ResetFish() override {}
    void WatchFish(int) override {}
    void HookFish(int fish_no) override { Record(std::format("hook fish {}", fish_no)); }
    bool ModelReady() override { return model_ready; }
    void LandFish() override { Record("land fish"); }
    void PutAwayFish() override { Record("put away"); }
    std::optional<fishing::Catch> Caught() override { return caught; }
    bool                          MessageDone() override { return message_done; }
    const fishing::Place         &CurrentPlace() override { return *place; }
    fishing::BiteContext          Bait() override {
        return {.bait = "vanilla:carrot", .radius = 25.0f, .random = [this] { return Random(); }};
    }
    fishing::OutcomeContext Outcome() override {
        return {.add_points = [this](int added) { points += added; },
                .record = [this](int kind, int size) { records.insert(records.end(), {kind, size}); },
                .count_mardan_garayan = [this] { mardan_garayan++; },
                .show = [this](const fishing::Catch &) { shown++; }};
    }
    double Random() override {
        if (rolls.empty()) {
            return 0.5;
        }
        const double roll = rolls.front();
        rolls.pop_front();
        return roll;
    }

    bool Did(const std::string &call) const { return std::ranges::find(calls, call) != calls.end(); }

private:
    void Record(std::string call) {
        // The idle motions repeat every tick; one line for each run of them.
        if (calls.empty() || calls.back() != call) {
            calls.push_back(std::move(call));
        }
    }
};

class FishingSession : public ::testing::Test {
protected:
    fishing::Session session;
    FakeAngler       angler;

    static void SetUpTestSuite() {
        item::Load();
        fishing::Load();
    }

    void Tick(const fishing::Input &input = {}) { session.Tick(angler, input); }

    // Stands, casts, and waits for a bite with the float out.
    void CastAndWait() {
        Tick();
        ASSERT_EQ(session.CurrentStep(), Step::Stand);
        Tick({.cross = true});
        Tick();
        ASSERT_EQ(session.CurrentStep(), Step::Casting);
        angler.motion_done = true;
        Tick();
        angler.motion_done = false;
        ASSERT_EQ(session.CurrentStep(), Step::WaitBite);
    }
};

TEST_F(FishingSession, ACatchGoesThroughEveryStepAsRetail) {
    CastAndWait();
    Tick({.fish_status = 7, .fish_no = 3});
    EXPECT_EQ(session.CurrentStep(), Step::BiteCheck);
    Tick({.fish_status = 8, .fish_no = 3});
    EXPECT_EQ(session.CurrentStep(), Step::Hooked);
    EXPECT_TRUE(angler.Did("bite cue"));
    Tick({.cross = true, .fish_status = 8, .fish_no = 3});
    EXPECT_EQ(session.CurrentStep(), Step::Battle);
    EXPECT_TRUE(angler.Did("motion 12 2") && angler.Did("hook fish 3"));

    // The fight lasts its 120 ticks and then waits for the model.
    for (int tick = 0; tick <= 121; tick++) {
        Tick({.fish_status = 11});
        ASSERT_EQ(session.CurrentStep(), Step::Battle) << tick;
    }
    angler.model_ready = true;
    Tick({.fish_status = 11});
    EXPECT_EQ(session.CurrentStep(), Step::Landed);
    EXPECT_TRUE(angler.Did("bait used") && angler.Did("land fish") && angler.Did("motion 6 6"));

    Tick();
    EXPECT_EQ(session.CurrentStep(), Step::Result);
    EXPECT_TRUE(angler.Did("motion 10 6") && angler.Did("music out"));
    angler.motion_done = true;
    Tick();
    EXPECT_TRUE(angler.Did("motion 8 0"));
    angler.motion_done = false;

    // Retail's evaluation: the Mardan Garayan, the points, the records, the message.
    Tick();
    EXPECT_EQ(angler.mardan_garayan, 1);
    EXPECT_EQ(angler.points, 300);
    EXPECT_EQ(angler.records, (std::vector<int>{5, 142}));
    EXPECT_EQ(angler.shown, 1);
    Tick();
    EXPECT_EQ(session.CurrentStep(), Step::Result); // the message is still open
    angler.message_done = true;
    Tick();
    Tick();
    EXPECT_EQ(session.CurrentStep(), Step::Finish);
    EXPECT_TRUE(angler.Did("music in") && angler.Did("motion 9 6"));
    for (int tick = 0; tick < 121; tick++) {
        Tick();
    }
    EXPECT_TRUE(angler.Did("put away"));
    angler.motion_done = true;
    Tick();
    EXPECT_EQ(session.CurrentStep(), Step::Init);
    EXPECT_FALSE(angler.Did("bait lost"));
}

TEST_F(FishingSession, CrossOnANibbleSinksTheFloatAndLosesTheBaitOneTimeInFive) {
    for (const auto &[roll, lost] : std::vector<std::pair<double, bool>>{{0.19, true}, {0.21, false}}) {
        session.Reset();
        angler.calls.clear();
        CastAndWait();
        Tick({.fish_status = 7});
        angler.rolls = {roll};
        Tick({.cross = true, .fish_status = 7});
        EXPECT_EQ(session.CurrentStep(), Step::Sink);
        EXPECT_TRUE(angler.Did("motion 6 6") && angler.Did(std::format("sound {}", static_cast<int>(fishing::Sound::Sink))));
        EXPECT_EQ(angler.Did("bait lost"), lost) << roll;
        angler.motion_done = true;
        Tick();
        EXPECT_EQ(session.CurrentStep(), Step::Stand);
        angler.motion_done = false;
    }
}

TEST_F(FishingSession, AFishThatLetsGoTakesTheBaitThreeTimesInTen) {
    for (const auto &[roll, lost] : std::vector<std::pair<double, bool>>{{0.29, true}, {0.31, false}}) {
        session.Reset();
        angler.calls.clear();
        CastAndWait();
        Tick({.fish_status = 8});
        ASSERT_EQ(session.CurrentStep(), Step::Hooked);
        angler.rolls = {roll};
        Tick({.fish_status = -1});
        EXPECT_EQ(session.CurrentStep(), Step::WaitBite);
        EXPECT_EQ(angler.Did("bait lost"), lost) << roll;
    }
}

TEST_F(FishingSession, CrossWhileWaitingReelsIn) {
    CastAndWait();
    Tick({.cross = true});
    EXPECT_EQ(session.CurrentStep(), Step::ReelIn);
    EXPECT_TRUE(angler.Did("motion 7 6"));
    for (int tick = 0; tick < 30; tick++) {
        Tick();
        EXPECT_LT(session.FloatWeight(), 0.0f);
    }
    Tick();
    EXPECT_NEAR(session.FloatWeight(), 0.02f, 1e-6f);
    angler.motion_done = true;
    Tick();
    EXPECT_EQ(session.CurrentStep(), Step::Stand);
}

TEST_F(FishingSession, CircleAndSquareOpenTheTownsMenus) {
    Tick();
    Tick({.circle = true});
    Tick({.square = true});
    EXPECT_TRUE(angler.Did("menu 133") && angler.Did("menu 134"));
    EXPECT_EQ(session.CurrentStep(), Step::Stand);
}

TEST_F(FishingSession, ThePlacesBiteHoldsTheHookForItsTicks) {
    const fishing::BiteStage &bite = *angler.place->bite;
    fishing::FishView         fish{.species = fishing::SpeciesOfKind(9), .action = fishing::FishAction::Eat, .distance = 0.0f};
    std::deque<double>        rolls;
    const fishing::BiteContext context{.bait = "vanilla:carrot", .radius = 25.0f, .random = [&] {
                                           const double roll = rolls.front();
                                           rolls.pop_front();
                                           return roll;
                                       }};
    // Timer, the bite (kind 9's liking for the Carrot is 1, so x 0.5), not leaving, the hold.
    rolls = {0.0, 0.0, 0.5, 0.0};
    EXPECT_EQ(bite.Update(fish, context).ticks, 30);
    rolls = {0.0, 0.0, 0.5, 0.9999};
    const fishing::ActionStep longest = bite.Update(fish, context);
    EXPECT_EQ(longest.action, fishing::FishAction::Bite);
    EXPECT_EQ(longest.ticks, 60);
    // A fish too far from the bait can't take it, and a bare hook sends it away.
    fish.distance = 2.0f;
    rolls = {0.0, 0.5};
    EXPECT_EQ(bite.Update(fish, context).action, fishing::FishAction::Eat);
    rolls = {0.0};
    EXPECT_EQ(bite.Update(fish, {.random = context.random}).action, fishing::FishAction::Leave);
}

} // namespace

} // namespace dc::test
