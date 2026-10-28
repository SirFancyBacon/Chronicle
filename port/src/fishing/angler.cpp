// The fishing session's game side: the angler, the line and the fish, the camera, the sounds and the
// messages, as EdMoveChara's fishing block drove them.

// nlohmann/json (through the framework) before the game headers, whose one-letter macros it cannot stand.
#include "fishing/session.hpp"

#include <cstdlib>
#include <optional>

#include "camerafollow.hpp"
#include "character.hpp"
#include "clsmes.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "ebattle.hpp"
#include "edit.hpp"
#include "editloop.hpp"
#include "effectmacro.hpp"
#include "fish.hpp"
#include "fishing.hpp"
#include "fishing/host.hpp"
#include "gamepad.hpp"
#include "mainselect.hpp"
#include "item/framework.hpp"
#include "item/host.hpp"
#include "localize/localize.hpp"
#include "mathutil.hpp"
#include "savedata.hpp"
#include "shop.hpp"
#include "snd.hpp"
#include "sound_effect.h"
#include "text/gametext.hpp"

namespace dc::fishing {

using namespace dc::game;

namespace {

// The message a custom species' name is laid over the evaluation window as; the town's files stop
// far below it.
constexpr int kCustomNameMessage = 0x7F00;

double Uniform() {
    return static_cast<double>(std::rand()) / (static_cast<double>(RAND_MAX) + 1.0);
}

// The bait on the hook, as FishingStepFish gives it to the fish.
BiteContext HookBait() {
    BiteContext context{.random = Uniform};
    if (const BaitInfo *bait = BaitAt(esa_type); bait != nullptr && EsaFrame != nullptr) {
        context.bait = bait->id;
        context.radius = bait->radius;
        context.default_affinity = bait->default_affinity;
        // Debug builds let every fish notice the bait from anywhere.
        if (DebugMode) {
            context.radius = 100000000.0f;
        }
    }
    return context;
}

FishAction ActionOf(game::FishAction action) {
    switch (action) {
        case FISH_ACTION_APPROACH_FOOD:
            return FishAction::Approach;
        case FISH_ACTION_EAT_FOOD:
            return FishAction::Eat;
        case FISH_ACTION_BITE_HOOK:
            return FishAction::Bite;
        case FISH_ACTION_LEAVE:
            return FishAction::Leave;
        default:
            return FishAction::None;
    }
}

game::FishAction ToGame(FishAction action) {
    switch (action) {
        case FishAction::Approach:
            return FISH_ACTION_APPROACH_FOOD;
        case FishAction::Eat:
            return FISH_ACTION_EAT_FOOD;
        case FishAction::Bite:
            return FISH_ACTION_BITE_HOOK;
        case FishAction::Leave:
            return FISH_ACTION_LEAVE;
        case FishAction::None:
            break;
    }
    return FISH_ACTION_NONE;
}

class GameAngler final : public Angler {
public:
    GameAngler(CMainChara &chara, CCameraFollow &camera, float *position) : chara_(chara), camera_(camera), position_(position) {}

    void Motion(int motion, int flags) override { chara_.SetMotion(motion, flags); }

    int CurrentMotion() override { return chara_.motion_no; }

    bool MotionDone() override { return chara_.motion_state == 3; }

    void HoldStill() override { chara_mode = 1; }

    void Turn(float stick) override {
        sceVu0FVECTOR facing;
        chara_.GetRotation(facing);
        facing[1] = AngleInterpolate(facing[1], facing[1] - 0.1f * stick, 0.01f, INTERPOLATE_STEP);
        chara_.SetRotation(facing);
    }

    void Play(Sound sound) override {
        sceVu0FVECTOR float_pos;
        switch (sound) {
            case Sound::Cast:
                SndSePlay(SE_FISHING_CAST, position_, -1.0f, -1.0f);
                break;
            case Sound::Sink:
                FishLineGetUki(float_pos);
                SndSePlay(SE_FISHING_SINK, float_pos, -1.0f, -1.0f);
                break;
            case Sound::Bite:
                SndSePlay(SE_FISHING_BITE, -1, 0);
                break;
            case Sound::Landed:
                SndSePlay(SE_FISHING_LANDED, -1, 0);
                break;
            case Sound::Fanfare:
                SndSPSePlay(0x2F, -1);
                break;
        }
    }

    void Rumble(int intensity, int ticks) override { GamePad.SetVibration(1, intensity, ticks); }

    void Ripple(float radius) override {
        sceVu0FVECTOR ripple;
        FishLineGetUki(ripple);
        ripple[1] = FishingGetWaterLevel();
        EffectHamon(&EdEffectGroup, ripple, radius);
    }

    void PullFloat(float tension) override { FishPullHook(tension); }

    // Retail let the bite pass with a tug and a rumble much like a nibble's.
    void BiteCue() override {
        sceVu0FVECTOR ripple;
        FishLineGetUki(ripple);
        ripple[1] = FishingGetWaterLevel();
        SndSePlay(SE_FISHING_SPLASH, ripple, -1.0f, -1.0f);
        EffectHamon(&EdEffectGroup, ripple, 10.0f);
        EffectHamon(&EdEffectGroup, ripple, 15.0f);
        EffectHamon(&EdEffectGroup, ripple, 20.0f);
        GamePad.SetVibration(1, 255, 20);
    }

    void Help(bool waiting) override {
        if (waiting) {
            EdFishingAngleHelpMEs(FishingGetEsaItemNo());
        } else {
            EdFishingWalkHelpMes(FishingGetEsaItemNo());
        }
    }

    void OpenMenu(int event) override {
        EdMoveCharaInfo.event_no = event;
        EdMoveCharaInfo.system_event_no = -1;
        EdMoveCharaInfo.acted = true;
    }

    // The camera watches the point halfway between the angler and the float.
    void FollowLine() override {
        sceVu0FVECTOR centre;
        FishLineGetUki(centre);
        sceVu0AddVector(centre, centre, position_);
        sceVu0ScaleVector(centre, centre, 0.5f);
        camera_.SetFollow(centre[0], centre[1], centre[2]);
    }

    void CatchCamera(float distance) override {
        sceVu0FVECTOR facing;
        chara_.GetRotation(facing);
        camera_.SetAngle(AngleLimit(facing[1]));
        camera_.SetHeight(10.0f);
        camera_.SetDistance(distance);
        camera_.Step(-1);
    }

    void FadeMusic(bool out) override {
        static int volume = 0;
        if (out) {
            volume = SndGetBgmVol();
            SndBgmFadeOut(60, 0);
        } else {
            SndBgmFadeIn(60, volume, 0);
        }
    }

    bool FloatCaught() override { return FishingCheckUkiHook() != 0; }

    bool HookLow() override {
        sceVu0FVECTOR hook;
        FishLineGetHook(hook);
        return hook[1] < 5.0f + FishingGetWaterLevel();
    }

    void LoseBait() override {
        if (const BaitInfo *bait = BaitAt(esa_type); bait != nullptr && bait->lure) {
            return;
        }
        EdFishingLostEsaMes();
        FishingDeleteEsa();
    }

    // Even a lure comes off the hook: the landed fish's model goes where the script built the bait's
    // (_CLEAR_EVENT_BUFF puts the event buffer at the villagers' buffer's free end, as LandFish does).
    void UseBait() override {
        if (const BaitInfo *bait = BaitAt(esa_type); bait != nullptr && bait->lure) {
            EdGetItem(bait->item, 1, -1);
        }
        FishingDeleteEsa();
    }

    void ResetFish() override { FishingInitFishStatus(); }

    void WatchFish(int fish_no) override { FishingAngleFish(fish_no); }

    void HookFish(int fish_no) override {
        FishingBattleFish(fish_no);
        char *file = GetFishFileName(FishingFishKind(fish_no));
        if (file != nullptr) {
            StartReadBG();
            LoadFileBG(file, reinterpret_cast<u_long128 *>(read_buffer), nullptr);
        }
    }

    bool ModelReady() override { return ReadBGSync() == 0; }

    // The angling model goes in the free end of the villagers' buffer.
    void LandFish() override {
        BG_READ_INFO  *read_info = GetReadBGFile(0);
        u_int         *fish_data = nullptr;
        CDataAlloc2<1> arena(-1);
        if (read_info != nullptr) {
            fish_data = reinterpret_cast<u_int *>(read_info->buffer);
            int free_blocks = EdVillagerBuffer.limit - EdVillagerBuffer.used;
            arena.base = EdVillagerBuffer.base + static_cast<std::ptrdiff_t>(EdVillagerBuffer.used) * 16;
            arena.limit = free_blocks;
            arena.used = 0;
        }
        FishingBattleToAngleFish(fish_data, &arena);
    }

    void PutAwayFish() override { FishingDeleteAngleFish(); }

    std::optional<Catch> Caught() override {
        int       size = 0;
        int       points = 0;
        const int kind = FishingGetAngleFishSize(&size, &points);
        if (kind < 0) {
            return std::nullopt;
        }
        return Catch{.species = SpeciesOfKind(kind), .size = size, .points = points};
    }

    bool MessageDone() override {
        if (!message_shown) {
            return true;
        }
        if (EdMenuLoop(&EditMes1) == 0) {
            return false;
        }
        EditMes1.auto_pos = MES_POS_TOP_LEFT;
        ClsMesSetMessageOverlay(nullptr, nullptr);
        message_shown = false;
        return true;
    }

    const Species *FishSpecies(int fish_no) override {
        return Fish != nullptr && fish_no >= 0 && fish_no < kMaxFish ? SpeciesOfKind(Fish[fish_no].fish_kind) : nullptr;
    }

    const Species *NibblingSpecies() override {
        for (int fish_no = 0; Fish != nullptr && fish_no < kMaxFish; fish_no++) {
            if (Fish[fish_no].action == FISH_ACTION_EAT_FOOD) {
                return SpeciesOfKind(Fish[fish_no].fish_kind);
            }
        }
        return nullptr;
    }

    TimeOfDay Time() override { return TimeOfDayNow(); }

    const Place &CurrentPlace() override { return fishing::CurrentPlace(); }

    BiteContext Bait() override { return HookBait(); }

    OutcomeContext Outcome() override {
        return {.add_points = [](int points) { SaveData->AddFishingPoint(points); },
                .record =
                    [](const Catch &fish) {
                        // fish_data names a retail kind; a custom species' records are chronicle.dat's.
                        if (fish.species != nullptr && fish.species->IsCustom()) {
                            RecordCatch(fish);
                        } else if (fish.species != nullptr) {
                            SaveData->SetFishingRank(fish.species->kind, static_cast<float>(fish.size));
                        }
                    },
                .count_mardan_garayan =
                    [] {
                        SaveData->CountMardanGarayan();
                        SetFishMardanGarayanNum(1);
                    },
                .show =
                    [](const Catch &fish) {
                        EditMes1.mes_no[0] = GetFishMsgNo(fish.species != nullptr ? fish.species->kind : -1);
                        // A custom species' name has no message of the town's: it is laid over the window.
                        if (fish.species != nullptr && fish.species->IsCustom()) {
                            static text::GameTextFile names;
                            names.Set(kCustomNameMessage, SpeciesName(*fish.species, localize::LocalizeLanguageFile()));
                            ClsMesSetMessageOverlay(&EditMes1, names.Data());
                            EditMes1.mes_no[0] = kCustomNameMessage;
                        }
                        EditMes1.values[0] = fish.size;
                        EditMes1.values[1] = fish.points;
                        EditMes1.values[2] = SaveData->GetFishingPoint();
                        EditMes1.tail_on = false;
                        EditMes1.auto_pos = MES_POS_TOP_LEFT;
                        EditMes1.MakeMesWin(2000);
                        EditMes1.auto_pos = MES_POS_TOP_LEFT;
                        int window[4];
                        EditMes1.AutoSet(window);
                        message_shown = true;
                    }};
    }

    double Random() override { return Uniform(); }

private:
    CMainChara    &chara_;
    CCameraFollow &camera_;
    float         *position_;

    // Whether the evaluation opened its window, which the session waits on.
    static inline bool message_shown = false;
};

Session g_session;

} // namespace

void StepSession(CMainChara &chara, CCameraFollow &camera, float *position, float &float_weight, float &hook_weight) {
    // The game starts a session over by setting chara_fishing (EdMoveCharaInit, leaving fishing).
    if (chara_fishing == ED_FISHING_INIT && g_session.CurrentStep() != Step::Init) {
        g_session.Reset();
    }
    int          fish_no = -1;
    const Input  input{.cross = EdPadDown(0x40, 0xFFFF) != 0,
                       .circle = EdPadDown(0x20, 0xFFFF) != 0,
                       .square = EdPadDown(0x80, 0xFFFF) != 0,
                       .stick_x = GamePad.GetLXf(),
                       .fish_status = FishingFishStatus(&fish_no),
                       .fish_no = fish_no,
                       .debug = DebugMode != 0};
    GameAngler angler(chara, camera, position);
    g_session.Tick(angler, input);
    chara_fishing = static_cast<int>(g_session.CurrentStep());
    float_weight = g_session.FloatWeight();
    hook_weight = g_session.HookWeight();
}

// Retail's ANGLE and BATTLE actions are the session's; the rest are the bite stage's.
void UpdateFishAction(CFish &fish) {
    if (fish.action != FISH_ACTION_NONE && fish.action != FISH_ACTION_APPROACH_FOOD && fish.action != FISH_ACTION_EAT_FOOD &&
        fish.action != FISH_ACTION_BITE_HOOK && fish.action != FISH_ACTION_LEAVE) {
        fish.action_timer = 20 + std::rand() % 30;
        return;
    }
    sceVu0FVECTOR position;
    fish.GetPosition(position);
    BiteContext context{.random = Uniform};
    if (const BaitInfo *bait = BaitAt(fish.food_kind)) {
        context.bait = bait->id;
        context.radius = fish.food_radius;
        context.default_affinity = bait->default_affinity;
    }
    const FishView   view{.species = SpeciesOfKind(fish.fish_kind),
                          .action = ActionOf(fish.action),
                          .distance = DistVector(fish.food_position, position)};
    const ActionStep next = CurrentPlace().bite->Update(view, context);
    fish.action = ToGame(next.action);
    // Retail counts the timer down past 0 before asking again, and the tick asked counts too.
    fish.action_timer = next.ticks - 1;
}

} // namespace dc::fishing
