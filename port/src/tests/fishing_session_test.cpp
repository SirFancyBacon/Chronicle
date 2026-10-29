// nlohmann/json (through the framework) before the game headers, whose one-letter macros it cannot stand.
#include "fishing/session.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <random>
#include <deque>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "fishing/framework.hpp"
#include "fishing/host.hpp"
#include "fishing/quests.hpp"
#include "item/host.hpp"
#include "quest/framework.hpp"

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
    int                      shown_points = -1;
    std::vector<std::string> kept;
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
    void RestartBite() override { Record("restart bite"); }
    void WatchFish(int) override {}
    void HookFish(int fish_no) override { Record(std::format("hook fish {}", fish_no)); }
    bool ModelReady() override { return model_ready; }
    void LandFish() override { Record("land fish"); }
    void PutAwayFish() override { Record("put away"); }
    std::optional<fishing::Catch> Caught() override { return caught; }
    bool                          MessageDone() override { return message_done; }
    const fishing::Species       *FishSpecies(int fish_no) override { return fishing::SpeciesOfKind(fish_no == 3 ? 5 : 2); }
    const fishing::Species       *NibblingSpecies() override { return fishing::SpeciesOfKind(6); }
    fishing::TimeOfDay            Time() override { return fishing::TimeOfDay::Evening; }
    const fishing::Place         &CurrentPlace() override { return *place; }
    fishing::BiteContext          Bait() override {
        return {.bait = "vanilla:carrot", .radius = 25.0f, .random = [this] { return Random(); }};
    }
    fishing::OutcomeContext Outcome() override {
        return {.add_points = [this](int added) { points += added; },
                .record = [this](const fishing::Catch &fish) { records.insert(records.end(), {fish.species->kind, fish.size}); },
                .count_mardan_garayan = [this] { mardan_garayan++; },
                .show = [this](const fishing::Catch &fish) {
                    shown++;
                    shown_points = fish.points;
                },
                .keep = [this](const fishing::Catch &fish) {
                    if (fish.species == nullptr || fish.species->item.empty()) {
                        return false;
                    }
                    kept.push_back(fish.species->item);
                    return true;
                }};
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
public:
    void Tick(const fishing::Input &input = {}) { session.Tick(angler, input); }

protected:
    fishing::Session session;
    FakeAngler       angler;

    static void SetUpTestSuite() {
        item::Load();
        fishing::Load();
    }

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

// Hooks fish 3 (a Mardan Garayan, kind 5, as FakeAngler says) and lands it.
void LandFish(FishingSession &test, fishing::Session &session, FakeAngler &angler) {
    angler.model_ready = true;
    angler.message_done = true;
    test.Tick({.fish_status = 8, .fish_no = 3});
    test.Tick({.cross = true, .fish_status = 8, .fish_no = 3});
    for (int tick = 0; tick < 300 && session.CurrentStep() != Step::Init; tick++) {
        angler.motion_done = session.CurrentStep() == Step::Finish || angler.motion == 10;
        test.Tick({.fish_status = 11});
    }
    angler.motion_done = false;
}

TEST_F(FishingSession, ACatchIsPublishedOnceWithWhereAndWhatItTook) {
    std::vector<fishing::Catch> catches;
    std::string                 bait;
    const int id = fishing::OnCatch([&](const fishing::Catch &fish) {
        catches.push_back(fish);
        bait = std::string(fish.bait);
    });
    CastAndWait();
    LandFish(*this, session, angler);
    fishing::Unsubscribe(id);
    ASSERT_EQ(catches.size(), 1u);
    EXPECT_EQ(catches[0].species->id, "vanilla:fish_5");
    EXPECT_EQ(catches[0].size, 142);
    EXPECT_EQ(catches[0].points, 300);
    EXPECT_EQ(catches[0].place->id, "vanilla:matataki_2");
    EXPECT_EQ(bait, "vanilla:carrot");
    EXPECT_EQ(catches[0].time, fishing::TimeOfDay::Evening);
    EXPECT_FALSE(catches[0].kept);
}

TEST_F(FishingSession, AFishThatGetsAwayIsPublishedWithWhy) {
    std::vector<fishing::Lost> lost;
    const int id = fishing::OnLost([&](const fishing::Lost &fish) { lost.push_back(fish); });
    CastAndWait();
    Tick({.fish_status = 7});
    angler.rolls = {0.1};
    Tick({.cross = true, .fish_status = 7});
    angler.motion_done = true;
    Tick();
    angler.motion_done = false;
    Tick({.cross = true});
    Tick();
    angler.motion_done = true;
    Tick();
    angler.motion_done = false;
    ASSERT_EQ(session.CurrentStep(), Step::WaitBite);
    Tick({.fish_status = 8, .fish_no = 3});
    angler.rolls = {0.9};
    Tick({.fish_status = -1});
    fishing::Unsubscribe(id);
    ASSERT_EQ(lost.size(), 2u);
    EXPECT_EQ(lost[0].why, fishing::LossCause::PressedEarly);
    EXPECT_EQ(lost[0].species->kind, 6); // the nibbling fish, not FishingFishStatus's fish_no
    EXPECT_TRUE(lost[0].bait_lost);
    EXPECT_EQ(lost[1].why, fishing::LossCause::LetGo);
    EXPECT_EQ(lost[1].species->kind, 5);
    EXPECT_FALSE(lost[1].bait_lost);
}

TEST_F(FishingSession, QuestHelpersCountTheCatchesIntoTheQuestsNumbers) {
    quest::Clear();
    quest::Register(quest::Builder("chronicle:ruty").Stages({"fishing", "done"}).Build());
    fishing::CountCatches("chronicle:ruty", "mardan", {.species = "vanilla:fish_5"});
    fishing::CountCatches("chronicle:ruty", "big", {.min_size = 150});
    fishing::CountFishPoints("chronicle:ruty", "points");
    fishing::KeepLargest("chronicle:ruty", "largest");
    fishing::CountSpecies("chronicle:ruty", "matataki", {"vanilla:fish_2", "vanilla:fish_4", "vanilla:fish_5", "vanilla:fish_6"});
    for (int fish = 0; fish < 2; fish++) {
        CastAndWait();
        LandFish(*this, session, angler);
    }
    const quest::Progress &progress = *quest::FindProgress("chronicle:ruty");
    EXPECT_EQ(progress.vars.at("mardan"), 2);
    EXPECT_FALSE(progress.vars.contains("big")); // 142 is under 150
    EXPECT_EQ(progress.vars.at("points"), 600);
    EXPECT_EQ(progress.vars.at("largest"), 142);
    EXPECT_EQ(progress.vars.at("matataki"), 1); // the same species twice counts once
    quest::Clear();
}

TEST_F(FishingSession, AKeepPlaceGivesTheFishAsItsSpeciesItem) {
    // Matataki's second spot with its outcome turned to keep, and the Mardan Garayan made an item.
    fishing::Species mardan = *fishing::SpeciesOfKind(5);
    mardan.item = "vanilla:flapping_fish";
    fishing::Place place = *angler.place;
    for (const auto &[points, records] : std::vector<std::pair<bool, bool>>{{false, false}, {true, true}}) {
        place.outcome = fishing::MakeOutcomeStage({{"stage", "keep"}, {"points", points}, {"records", records}});
        angler = FakeAngler();
        angler.place = &place;
        angler.caught->species = &mardan;
        session.Reset();
        std::vector<bool> published;
        const int id = fishing::OnCatch([&](const fishing::Catch &fish) { published.push_back(fish.kept); });
        CastAndWait();
        LandFish(*this, session, angler);
        fishing::Unsubscribe(id);
        EXPECT_EQ(angler.kept, std::vector<std::string>{"vanilla:flapping_fish"});
        EXPECT_EQ(published, std::vector<bool>{true});
        EXPECT_EQ(angler.points, points ? 300 : 0);
        EXPECT_EQ(angler.records.empty(), !records);
        EXPECT_EQ(angler.mardan_garayan, records ? 1 : 0);
        EXPECT_EQ(angler.shown_points, points ? 300 : 0) << "the window says what was given";
    }

    // A species without an item isn't kept; the window still shows the catch.
    place.outcome = fishing::MakeOutcomeStage({{"stage", "keep"}});
    angler = FakeAngler();
    angler.place = &place;
    session.Reset();
    CastAndWait();
    LandFish(*this, session, angler);
    EXPECT_TRUE(angler.kept.empty());
    EXPECT_EQ(angler.shown, 1);
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

nlohmann::json Brisk() {
    return {
        {"stage",           "brisk"                                                                                     },
        {"wait",            {240, 480}                                                                                  },
        {"retry",           600                                                                                         },
        {"nothing",         1.0                                                                                         },
        {"nibble",          {{"low", {100, 160}}, {"normal", {50, 100}}, {"high", {30, 60}}}                             },
        {"window",          {40, 75}                                                                                    },
        {"lose_bait_early", 0.2                                                                                         }
    };
}

TEST_F(FishingSession, BriskChoosesOneFishInTheWaterByItsLikingOrNothing) {
    const auto bite = fishing::MakeBiteStage(Brisk());
    // For the Potato Cake: kind 2 likes it 1 (high, x 1.5), kind 6 0.5 (normal, x 1), kind 1 not at all;
    // the second kind 2 is far off, and comes all the same.
    const std::vector<const fishing::Species *> water = {fishing::SpeciesOfKind(2), fishing::SpeciesOfKind(6), fishing::SpeciesOfKind(1),
                                                         fishing::SpeciesOfKind(2), nullptr};
    const std::vector<float>               distances = {10.0f, 20.0f, 5.0f, 40.0f, 0.0f};
    std::mt19937                           engine(7);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    std::map<int, double>                  chosen;
    constexpr int                          kRolls = 40000;
    for (int roll = 0; roll < kRolls; roll++) {
        fishing::BiteSession session;
        bite->Update({.species = water[0]}, {.bait = "vanilla:potato_cake", .radius = 25.0f, .random = [&] { return uniform(engine); },
                                             .fish_index = 0, .fish = water, .distances = distances, .session = &session});
        chosen[session.chosen] += 1.0 / kRolls;
    }
    // Weights 1.5, 1, 0 and 1.5 against nothing's 1.
    EXPECT_NEAR(chosen[0], 1.5 / 5.0, 0.01);
    EXPECT_NEAR(chosen[1], 1.0 / 5.0, 0.01);
    EXPECT_NEAR(chosen[3], 1.5 / 5.0, 0.01);
    EXPECT_NEAR(chosen[-1], 1.0 / 5.0, 0.01);
    EXPECT_EQ(chosen.count(2), 0u);
}

TEST_F(FishingSession, BrisksChosenFishSwimsOverNibblesAndBitesForItsWindow) {
    const auto bite = fishing::MakeBiteStage(Brisk());
    const std::vector<const fishing::Species *> water = {fishing::SpeciesOfKind(2), fishing::SpeciesOfKind(6)};
    fishing::BiteSession session{.rolled = true, .chosen = 1};
    double               roll = 0.0;
    const auto           context = [&](int index, int now = 0) {
        return fishing::BiteContext{.bait = "vanilla:potato_cake", .radius = 25.0f, .random = [&] { return roll; }, .fish_index = index,
                                    .fish = water, .distances = {5.0f, 10.0f}, .session = &session, .now = now};
    };
    // Chosen while it was nibbling another time: it starts a wait of its own.
    fishing::FishView fish{.species = water[1], .action = fishing::FishAction::Eat, .distance = 10.0f};
    roll = 0.9999;
    const fishing::ActionStep start = bite->Update(fish, context(1));
    EXPECT_EQ(start.action, fishing::FishAction::Approach);
    EXPECT_EQ(start.ticks, 480);
    EXPECT_EQ(session.speed, 0.1f); // near: retail's speed covers it
    fish.action = fishing::FishAction::Approach;
    EXPECT_EQ(bite->Update(fish, context(1)).action, fishing::FishAction::Approach); // still swimming over
    fish.distance = 1.0f;
    const fishing::ActionStep nibble = bite->Update(fish, context(1));
    EXPECT_EQ(nibble.action, fishing::FishAction::Eat);
    EXPECT_EQ(nibble.ticks, 100); // kind 6's normal liking: 50 to 100
    fish.action = fishing::FishAction::Eat;
    EXPECT_EQ(bite->Update(fish, context(1)).action, fishing::FishAction::Bite);
    EXPECT_EQ(bite->Update(fish, context(1)).ticks, 75);
    roll = 0.0;
    EXPECT_EQ(bite->Update(fish, context(1)).ticks, 40);

    // The other fish leaves the bait alone.
    EXPECT_EQ(bite->Update({.species = water[0]}, context(0)).action, fishing::FishAction::None);

    // Missed, the chosen fish leaves and the next roll comes.
    fish.action = fishing::FishAction::Bite;
    EXPECT_EQ(bite->Update(fish, context(1)).action, fishing::FishAction::Leave);
    EXPECT_FALSE(session.rolled);
}

TEST_F(FishingSession, BrisksChosenFishSwimsFastEnoughToReachTheBaitInItsWait) {
    const auto bite = fishing::MakeBiteStage(Brisk());
    const std::vector<const fishing::Species *> water = {fishing::SpeciesOfKind(2)};
    for (const auto &[distance, speed] : std::vector<std::pair<float, float>>{{60.0f, 0.25f}, {200.0f, 0.6f}}) {
        fishing::BiteSession session{.rolled = true, .chosen = 0};
        bite->Update({.species = water[0], .distance = distance},
                     {.bait = "vanilla:potato_cake", .radius = 25.0f, .random = [] { return 0.0; }, .fish_index = 0, .fish = water,
                      .distances = {distance}, .session = &session});
        EXPECT_FLOAT_EQ(session.speed, speed) << distance; // a wait of 240, no faster than 0.6
    }
}

TEST_F(FishingSession, BrisksChosenFishThatCannotReachTheBaitSwimsOff) {
    const auto bite = fishing::MakeBiteStage(Brisk());
    const std::vector<const fishing::Species *> water = {fishing::SpeciesOfKind(2)};
    fishing::BiteSession session{.rolled = true, .chosen = 0};
    const auto context = [&](int now) {
        return fishing::BiteContext{.bait = "vanilla:potato_cake", .radius = 25.0f, .random = [] { return 0.0; }, .fish_index = 0, .fish = water,
                                    .distances = {20.0f}, .session = &session, .now = now};
    };
    fishing::FishView fish{.species = water[0], .distance = 20.0f};
    EXPECT_EQ(bite->Update(fish, context(0)).ticks, 240);
    fish.action = fishing::FishAction::Approach;
    EXPECT_EQ(bite->Update(fish, context(479)).action, fishing::FishAction::Approach);
    EXPECT_EQ(bite->Update(fish, context(480)).action, fishing::FishAction::Leave); // twice its wait, never at the bait
    EXPECT_FALSE(session.rolled);
}

TEST_F(FishingSession, BriskLooksAgainSoonWhileNoFishLikesTheBaitAndAfterItsRetryForNothing) {
    const auto bite = fishing::MakeBiteStage(Brisk());
    // Kind 5 likes only the Poisonous Apple.
    const std::vector<const fishing::Species *> water = {fishing::SpeciesOfKind(5)};
    fishing::BiteSession session;
    int                  rolls = 0;
    double               roll = 0.0;
    fishing::BiteContext context{.bait = "vanilla:carrot", .radius = 25.0f, .random = [&] {
                                     rolls++;
                                     return roll;
                                 }, .fish_index = 0, .fish = water, .distances = {5.0f}, .session = &session, .now = 100};
    EXPECT_EQ(bite->Update({.species = water[0]}, context).action, fishing::FishAction::None);
    EXPECT_EQ(session.chosen, -1);
    EXPECT_EQ(session.until, 130);
    EXPECT_EQ(rolls, 0); // nothing to roll for

    // Liked, but the roll comes up nothing (its weight 1 against 1.5): another after the retry.
    context.bait = "vanilla:poisonous_apple";
    context.now = 130;
    roll = 0.9;
    bite->Update({.species = water[0]}, context);
    EXPECT_EQ(session.chosen, -1);
    EXPECT_EQ(session.until, 730);
    context.now = 729;
    bite->Update({.species = water[0]}, context);
    EXPECT_EQ(rolls, 1);
    context.now = 730;
    roll = 0.0;
    bite->Update({.species = water[0]}, context);
    EXPECT_EQ(session.chosen, 0);
}

TEST_F(FishingSession, BriskLeavesTheBaitAloneUntilTheFloatIsOut) {
    const auto bite = fishing::MakeBiteStage(Brisk());
    const std::vector<const fishing::Species *> water = {fishing::SpeciesOfKind(2)};
    fishing::BiteSession session;
    const fishing::BiteContext context{.bait = "vanilla:potato_cake", .radius = 25.0f, .random = [] { return 0.0; }, .fish_index = 0,
                                       .fish = water, .distances = {5.0f}, .waiting = false, .session = &session};
    EXPECT_EQ(bite->Update({.species = water[0]}, context).action, fishing::FishAction::None);
    EXPECT_EQ(session.chosen, -1);
}

TEST_F(FishingSession, TheSessionStartsTheBiteOverAsTheFloatSettlesAndTheAnglerMoves) {
    CastAndWait();
    EXPECT_TRUE(angler.Did("restart bite"));
    angler.calls.clear();
    Tick({.stick_x = 0.5f});
    EXPECT_TRUE(angler.Did("restart bite"));
    angler.calls.clear();
    Tick({.cross = true});
    EXPECT_TRUE(angler.Did("restart bite"));
}

} // namespace

} // namespace dc::test
