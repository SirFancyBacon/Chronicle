#include "battlemenu.hpp"

// Port-specific headers
#include <SDL3/SDL.h>

// Retail headers
#include "btactstatus.hpp"
#include "camera.hpp"
#include "caption_text.hpp"
#include "character.hpp"
#include "clsmes.hpp"
#include "dataread.hpp"
#include "dngstatusdata.hpp"
#include "draw2d_port.hpp"
#include "dun/gameloop.hpp"
#include "eastking.hpp"
#include "gamepad.hpp"
#include "itemdata.hpp"
#include "localize.hpp"
#include "mainselect.hpp"
#include "map.hpp"
#include "memcard.hpp"
#include "menu_draw.hpp"
#include "menu_dungeon.hpp"
#include "menu_inventory.hpp"
#include "menu_manual.hpp"
#include "menu_misc.hpp"
#include "menu_mouse.hpp"
#include "menu_save.hpp"
#include "menuetc.hpp"
#include "mglib.hpp"
#include "rect.hpp"
#include "snd.hpp"
#include "texture.hpp"
#include "userstatus.hpp"
#include "weaponlevelup.hpp"

// UI Framework
#include "ui_framework.hpp"

#include <algorithm>
#include <string>
#include <vector>

extern void Btl_InitMenuChara(u_long128 *buffer);
extern void Btl_InitWeaponSelect(int mode, int chara);
extern void Btl_InitItemMode(int mode, int chara);
extern void Btl_BattleManualInit(int*, u_long128*);
extern void BtlMenuTexBlockEnter();
extern void DrawWorldMap(int alpha);
extern int  MapNoTransFunc(int map_no);

#define BTLMENU_PAGE_QUIT 100

namespace {

bool       s_quit_confirm_active = false;
int        s_quit_confirm_choice = 1;
UI::Cursor s_quit_cursor;
char       g_frame_image_name[]  = "frame_image";

int GetMenuModeMax() {
    return (BtlMenuMode == BATTLE_MENU_MODE_DUNGEON) ? 8 : 9;
}

void BtlMenuMekeIconInfo(int *icons, int /*menu_mode*/) {
    constexpr s16 kCustomDrawTbl[2][9] = {
        {0, 1, 2, 3, 7, 4, 6, 12, 0}, // Dungeon (6: Journal/Book, 12: Quit/House)
        {0, 1, 2, 3, 8, 5, 4,  6, 12}  // Town    (6: Journal/Book, 12: Quit/House)
    };

    const int icon_count = GetMenuModeMax();
    for (int i = 0; i < icon_count; i++) {
        icons[i] = kCustomDrawTbl[BtlMenuMode][i];
    }

    if (BtlMenuMode != BATTLE_MENU_MODE_DUNGEON) {
        switch (NowGetGameFlagForBtlMenu(BtlMenuMode)) {
            case MENU_MOVE_LOCKED:
            case MENU_MOVE_FIRST_DUNGEON: icons[4] = 9;  break;
            case MENU_MOVE_WORLD_MAP:     icons[4] = 8;  break;
            case MENU_MOVE_INTERIOR_OUT:  icons[4] = 12; break;
            default:                      icons[4] = 0;  break;
        }
    }
}

void GetSafeIconPos(int index, float& out_x, float& out_y) {
    const int orig_max = (BtlMenuMode == BATTLE_MENU_MODE_DUNGEON) ? 7 : 8;
    if (index < orig_max) {
        out_x = NorMenuIcon[index].x;
        out_y = NorMenuIcon[index].y;
    } else {
        const int diff = index - (orig_max - 1);
        out_x = NorMenuIcon[orig_max - 1].x - (16.0f * diff);
        out_y = NorMenuIcon[orig_max - 1].y + (40.0f * diff);
    }
}

struct HelpSpec {
    const char* title_key;
    const char* title_def;
    const char* desc_key;
    const char* desc_def;
};

void GetBattleMenuHelpData(int index, int mode, std::string& out_title, std::string& out_desc) {
    out_title.clear();
    out_desc.clear();

    static constexpr HelpSpec kCommonTop[4] = {
        {"battlemenu.item.title",    "\"Item\"",          "battlemenu.item.help",    "Use current item or\nSet active items."},
        {"battlemenu.weapon.title",  "\"Weapon\"",        "battlemenu.weapon.help",  "Change and customise\nweapons."},
        {"battlemenu.allies.title",  "\"Allies\"",        "battlemenu.allies.help",  "Change controllable\ncharacter."},
        {"battlemenu.georama.title", "\"Georama Parts\"", "battlemenu.georama.help", "Check acquired\ngeorama parts."},
    };
    static constexpr HelpSpec kSaveSpec    = {"battlemenu.save.title",    "\"Save\"",      "battlemenu.save.help",    "Record current\ngame progress."};
    static constexpr HelpSpec kOptionSpec  = {"battlemenu.option.title",  "\"Options\"",   "battlemenu.option.help",  "Change game\nconfigurations."};
    static constexpr HelpSpec kJournalSpec = {"battlemenu.journal.title", "\"Journal\"",   "battlemenu.journal.help", "Check manuals and\nchest tracker."};
    static constexpr HelpSpec kQuitSpec    = {"battlemenu.quit.title",    "\"Quit Game\"", "battlemenu.quit.help",    "Exit the game and\nreturn to desktop."};

    const HelpSpec* spec = nullptr;

    if (index >= 0 && index < 4) {
        spec = &kCommonTop[index];
    } else if (index == 4) {
        if (mode == BATTLE_MENU_MODE_DUNGEON) {
            if (GetEscapeDngFlag() == 2) {
                out_title = LocalizeText("battlemenu.escape.title", "\"Escape\"");
                out_desc  = LocalizeText("battlemenu.escape.help", "Escape from the\ncurrent dungeon.");
            } else {
                out_title = LocalizeText("battlemenu.go_outside.title", "\"Go Outside\"");
                out_desc  = LocalizeText("battlemenu.go_outside.help", "Go outside of\nthe building.");
            }
        } else {
            switch (NowGetGameFlagForBtlMenu(BtlMenuMode)) {
                case MENU_MOVE_WORLD_MAP:
                    out_title = LocalizeText("battlemenu.world_map.title", "\"World Map\"");
                    out_desc  = LocalizeText("battlemenu.world_map.help", "Move to another\narea on world map.");
                    break;
                case MENU_MOVE_INTERIOR_OUT:
                    out_title = LocalizeText("battlemenu.go_outside.title", "\"Go Outside\"");
                    out_desc  = LocalizeText("battlemenu.go_outside.help", "Go outside of\nthe building.");
                    break;
                default:
                    out_title = LocalizeText("battlemenu.dungeon.title", "\"Go to Dungeon\"");
                    out_desc  = LocalizeText("battlemenu.dungeon.help", "Go to Divine\nBeast Cave.");
                    break;
            }
        }
        return;
    } else if (mode == BATTLE_MENU_MODE_DUNGEON) {
        if (index == 5)      spec = &kOptionSpec;
        else if (index == 6) spec = &kJournalSpec;
        else if (index == 7) spec = &kQuitSpec;
    } else {
        if (index == 5)      spec = &kSaveSpec;
        else if (index == 6) spec = &kOptionSpec;
        else if (index == 7) spec = &kJournalSpec;
        else if (index == 8) spec = &kQuitSpec;
    }

    if (spec != nullptr) {
        out_title = LocalizeText(spec->title_key, spec->title_def);
        out_desc  = LocalizeText(spec->desc_key,  spec->desc_def);
    }
}

std::string GetBattleMenuCaption(int index, int mode) {
    std::string title, desc;
    GetBattleMenuHelpData(index, mode, title, desc);
    if (title.size() >= 2 && title.front() == '"' && title.back() == '"') {
        title = title.substr(1, title.size() - 2);
    }
    return title;
}

void DrawBtlMenuBar() {
    int alpha = 128;
    if (BtlEffectFlag == BTLEFFECT_PAGE_CLOSE || BattleMenuFlag == BTLMENU_STATE_APPEAR) {
        alpha = std::min(128, static_cast<int>(10.0f * BtlEffectCt));
    } else if (BtlEffectFlag == BTLEFFECT_PAGE_OPEN || BattleMenuFlag == BTLMENU_STATE_EXIT) {
        alpha = std::max(0, static_cast<int>(128.0f - 10.0f * BtlEffectCt));
    }

    NowGetGameFlagForBtlMenu(BtlMenuMode);
    const int icon_count = GetMenuModeMax();
    int       icons[9]   = {};
    BtlMenuMekeIconInfo(icons, BtlMenuMode);

    for (int i = 0; i < icon_count; i++) {
        int       icon_alpha = alpha;
        const int brightness = (MenuWarningMsgFlag != 0) ? 64 : 128;
        const int menu_flag  = BattleMenuFlag;

        if (menu_flag != BTLMENU_STATE_MAIN && menu_flag != BTLMENU_STATE_APPEAR && menu_flag != BTLMENU_STATE_EXIT) {
            if (i != MenuSelect[1]) {
                continue;
            }
        }

        float icon_x = 0.0f, icon_y = 0.0f;
        GetSafeIconPos(i, icon_x, icon_y);

        int selected = 0;
        int x        = static_cast<int>(icon_x);
        int y        = static_cast<int>(icon_y + 3.0f);

        if (i == MenuSelect[1]) {
            selected   = 1;
            x          = static_cast<int>(icon_x - 10.0f);
            y          = static_cast<int>(icon_y - 1.0f);
            icon_alpha = 128;
        }

        if (icons[i] >= 0) {
            caption::SetNextCaptionText(GetBattleMenuCaption(i, BtlMenuMode));
            DrawMainMenuIcon(x, y, icons[i], selected, brightness, icon_alpha);
        }
    }
}

int BtlMenuDrawSpecialFlag(int flag) {
    if ((BattleMenuFlag == BTLMENU_STATE_ATLA_OPEN || BattleMenuFlag == BTLMENU_STATE_ATLA ||
         BattleMenuFlag == BTLMENU_STATE_ATLA_CLOSE) && GetMenuAtraEventFlag() != 0) {
        flag = 0;
    }
    if (MenuChara.state == MENU_CHARA_CHANGE_LOAD || MenuChara.state == MENU_CHARA_CHANGE_EXIT) {
        flag = 0;
    }
    return flag;
}

void DrawQuitConfirmModal(int alpha) {
    UI::ConfirmModal::Draw(
        LocalizeText("battlemenu.quit.confirm_title", "\"Quit Game\""),
        LocalizeText("battlemenu.quit.confirm_desc",  "Return to desktop?"),
        LocalizeText("battlemenu.quit.yes",           "YES"),
        LocalizeText("battlemenu.quit.no",            "NO"),
        s_quit_confirm_choice,
        s_quit_cursor,
        alpha
    );
}

UI::Cursor s_move_cursor;
int        s_last_move_state = -1;

void ExtractMovePromptText(std::string& out_title, std::string& out_desc) {
    out_title.clear();
    out_desc.clear();

    if (CommonMenuMes3.buff != nullptr && CommonMenuMes3.mes_made > 0) {
        if (const short* codes = CommonMenuMes3.GetTextLineDataTop(CommonMenuMes3.mes_made)) {
            std::string decoded = GameTextDecode(codes);

            // Strip control tags like {wait}, {page}, or trailing newlines
            for (size_t pos = 0; (pos = decoded.find('{', pos)) != std::string::npos;) {
                size_t end = decoded.find('}', pos);
                if (end != std::string::npos) {
                    decoded.erase(pos, end - pos + 1);
                } else {
                    break;
                }
            }
            while (!decoded.empty() && (decoded.back() == '\n' || decoded.back() == '\r' || decoded.back() == ' ')) {
                decoded.pop_back();
            }

            const size_t nl = decoded.find('\n');
            if (nl != std::string::npos) {
                out_title = decoded.substr(0, nl);
                out_desc  = decoded.substr(nl + 1);
            } else {
                out_title = decoded;
            }
        }
    }

    if (out_title.empty()) {
        switch (MenuMove.mode) {
            case MENU_MOVE_FIRST_DUNGEON:
                out_title = LocalizeText("battlemenu.move.enter_dungeon_1", "Enter");
                out_desc  = LocalizeText("battlemenu.move.enter_dungeon_2", "Divine Beast Cave?");
                break;
            case MENU_MOVE_INTERIOR_OUT:
                out_title = LocalizeText("battlemenu.move.go_outside", "Go outside?");
                break;
            case MENU_MOVE_DUNGEON_ESCAPE:
                out_title = LocalizeText("battlemenu.move.leave_dungeon", "Leave the dungeon?");
                break;
            default:
                break;
        }
    }
}

} // namespace

PORT_OVERRIDE void DrawBattleMain() {
    int icons[9] = {};
    int next     = MenuSelect[1] + 1;
    int pos_x    = next * 16 + 80;
    int pos_y    = MenuSelect[1] * 40 + 54;
    if (next > 4) {
        pos_x -= ((MenuSelect[1] - 4) << 5) + 16;
    }

    SysCur[0] += (static_cast<float>(pos_x - 0x2C) - SysCur[0]) / 4.0f;
    SysCur[1] += (static_cast<float>(pos_y) - SysCur[1]) / 4.0f;

    BtlMenuMekeIconInfo(icons, BtlMenuMode);
    MENU_ICON_INFO *info = GetMenuIconInfo(icons[MenuSelect[1]]);

    BtlWakuMake2              = 1;
    CommonMenuMes2.mes_made   = -1;
    CommonMenuMes2.stay_frame = false;

    if (BtlMenuMode == BATTLE_MENU_MODE_DUNGEON) {
        int limit = GetLimmitMsg();
        if (CommonMenuMes1.mes_made != limit) {
            CommonMenuMes1.MakeMesWin(limit);
        }
        if (limit >= 0) {
            MenuTextureReload(CommonMenuMes1.tex_block);
            CommonMenuMes1.text_x     = 0x50;
            CommonMenuMes1.text_y     = 0x14A;
            CommonMenuMes1.stay_frame = true;
            CommonMenuMes1.Step();
            CommonMenuMes1.DrawMesWin();
        }
    }

    if (!s_quit_confirm_active) {
        UI::Label cap_lbl(GetBattleMenuCaption(MenuSelect[1], BtlMenuMode));
        int       width = cap_lbl.GetWidth() + 0x4A;
        if (info != nullptr && width < (info->label_width + 0x4A)) {
            width = info->label_width + 0x4A;
        }

        float icon_x = 0.0f, icon_y = 0.0f;
        GetSafeIconPos(MenuSelect[1], icon_x, icon_y);

        DrawMenuWaku(icon_x - 18.0f, icon_y - 8.0f, width, 0x22, 0, StayTex, 0x80);
        DrawMenuObjectVibe(static_cast<int>(SysCur[0]), static_cast<int>(SysCur[1]), 1, 0x40);
    }
}

PORT_OVERRIDE void BattleMenuDraw() {
    MenuMouseNoteBattleMenu();

    setbilinear(0);
    MenuWorldTrans(&MenuCamera);
    MenuTextureReload(BtlMenuReadBlock);

    CTexture *raw_frame = TexManager.GetTexture(g_frame_image_name, -1);
    if (raw_frame == nullptr) {
        return;
    }
    CTexture frame = *raw_frame;
    reinterpret_cast<sceGsTex0 *>(&frame.tex0)->bits.tcc = 0;

    int bright = 0x40;
    if (BattleMenuFlag == BTLMENU_STATE_APPEAR) {
        bright = std::max(0x40, static_cast<int>(128.0f - 4.0f * BtlEffectCt));
    } else if (BattleMenuFlag == BTLMENU_STATE_EXIT) {
        bright = std::min(0x80, static_cast<int>(bright + 4.0f * BtlEffectCt));
    }

    DrawMenu2DSprite(&frame, MenuDispRc, MenuDispRc, bright, bright, bright, 0x80);

    switch (BattleMenuFlag) {
        case BTLMENU_STATE_MAIN:
            DrawBattleMain();
            break;
        case BTLMENU_STATE_CHARA:
        case BTLMENU_STATE_CHARA_OPEN:
        case BTLMENU_STATE_CHARA_CLOSE:
        case BTLMENU_STATE_UNK_1A:
            DrawCharaSelect();
            break;
        case BTLMENU_STATE_WEAPON:
        case BTLMENU_STATE_WEAPON_OPEN:
        case BTLMENU_STATE_WEAPON_CLOSE:
            WeaponMenuDraw();
            break;
        case BTLMENU_STATE_ITEM:
        case BTLMENU_STATE_ITEM_OPEN:
        case BTLMENU_STATE_ITEM_CLOSE:
            ItemMenuModeDraw();
            break;
        case BTLMENU_STATE_ATLA:
        case BTLMENU_STATE_ATLA_OPEN:
        case BTLMENU_STATE_ATLA_CLOSE:
            DrawMenuAtoraSelect();
            break;
        case BTLMENU_STATE_MOVE:
        case BTLMENU_STATE_MOVE_OPEN:
        case BTLMENU_STATE_MOVE_CLOSE:
            DrawMenuMove();
            break;
        case BTLMENU_STATE_OPTION:
        case BTLMENU_STATE_OPTION_OPEN:
        case BTLMENU_STATE_OPTION_CLOSE:
            DrawMenuOption();
            setbilinear(0);
            break;
        case BTLMENU_STATE_SAVE:
        case BTLMENU_STATE_SAVE_OPEN:
        case BTLMENU_STATE_SAVE_CLOSE:
            DrawMenuSave(g_frame_image_name);
            setbilinear(0);
            break;
        case BTLMENU_STATE_MANUAL:
        case BTLMENU_STATE_MANUAL_OPEN:
        case BTLMENU_STATE_MANUAL_CLOSE:
            MenuManualDraw();
            break;
    }

    CursorVibeCnt = (CursorVibeCnt + 1 >= 0x405F7E00) ? 0 : (CursorVibeCnt + 1);

    const int draw_help = BtlMenuDrawSpecialFlag(BtlWakuMake2);
    if (BtlMenuReadEndFlag != 0) {
        if (BattleMenuFlag == BTLMENU_STATE_MAIN || BattleMenuFlag == BTLMENU_STATE_APPEAR) {
            BtlHelpWinAlpha = std::min(0x80, BtlHelpWinAlpha + 9);
        } else {
            BtlHelpWinAlpha = std::max(0, BtlHelpWinAlpha - 8);
        }
    }

    int text_x = 0, text_y = 0;
    GetMainMenuRightHelpMsgLangOffset(text_x, text_y);

    if (BtlMenuReadEndFlag != 0 && draw_help != 0 && BattleMenuFlag == BTLMENU_STATE_MAIN &&
        BtlHelpWinAlpha > 0 && !s_quit_confirm_active) {
        std::string title, desc;
        GetBattleMenuHelpData(MenuSelect[1], BtlMenuMode, title, desc);

        constexpr int box_w = 276;
        const int     box_x = (HelpWinHead[0] > 0) ? static_cast<int>(HelpWinHead[0]) : (640 - box_w - 20);
        UI::HelpBox::DrawTitled(box_x, 452, box_w, text_x, title, desc, BtlHelpWinAlpha);
    }

    setbilinear(0);
    if (BtlMenuDrawSpecialFlag(1) != 0) {
        DrawBtlMenuBar();
    }

    if (WepMenu.mode != WEP_MENU_BUILDUP_SELECT) {
        MenuWepLevelUp.Step();
        MenuWepLevelUp.Draw();
    }

    if (s_quit_confirm_active && BtlHelpWinAlpha > 0) {
        DrawQuitConfirmModal(BtlHelpWinAlpha);
    }

    MenuMouseDrawOverlay();

    if (GetInteriorOutFlag() != 0) {
        BtlEffectCt += 1.0f;
        AllFadeForMenu(std::min(0x80, static_cast<int>(3.0f * BtlEffectCt)));
    }

    setbilinear(1);
}

PORT_OVERRIDE int BattleMenuSelect() {
    if (MenuWarningMsgFlag != 0) {
        if (GamePad.Down(PAD_CIRCLE | PAD_CROSS) != 0) {
            MenuWarningMsgFlag = 0;
        }
        return 1;
    }

    if (s_quit_confirm_active) {
        if (GamePad.Down(PAD_UP | PAD_DOWN)) {
            s_quit_confirm_choice = 1 - s_quit_confirm_choice;
            ComMenuSePlay(MENU_SOUND_CURSOR);
        } else if (GamePad.Down(PAD_CROSS)) {
            if (s_quit_confirm_choice == 0) {
                ComMenuSePlay(MENU_SOUND_CONFIRM);
                SDL_Event event{};
                event.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&event);
            } else {
                ComMenuSePlay(MENU_SOUND_REFUSE);
                s_quit_confirm_active = false;
            }
        } else if (GamePad.Down(PAD_CIRCLE)) {
            ComMenuSePlay(MENU_SOUND_REFUSE);
            s_quit_confirm_active = false;
        }
        return 1;
    }

    const int old = MenuSelect[1];
    const int max = GetMenuModeMax();

    if (GamePad.Down(PAD_UP | PAD_LEFT) != 0) {
        if (--MenuSelect[1] < 0) MenuSelect[1] = max - 1;
    }
    if (GamePad.Down(PAD_RIGHT | PAD_DOWN) != 0) {
        if (++MenuSelect[1] > max - 1) MenuSelect[1] = 0;
    }
    if (old != MenuSelect[1]) {
        ComMenuSePlay(MENU_SOUND_CURSOR);
    }

    if (GamePad.Down(PAD_CROSS)) {
        constexpr int kPages[2][9] = {
            {BTLMENU_PAGE_ITEM, BTLMENU_PAGE_WEAPON, BTLMENU_PAGE_CHARA, BTLMENU_PAGE_ATLA, BTLMENU_PAGE_MOVE, BTLMENU_PAGE_OPTION, BTLMENU_PAGE_MANUAL, BTLMENU_PAGE_QUIT, BTLMENU_PAGE_NONE},
            {BTLMENU_PAGE_ITEM, BTLMENU_PAGE_WEAPON, BTLMENU_PAGE_CHARA, BTLMENU_PAGE_ATLA, BTLMENU_PAGE_MOVE, BTLMENU_PAGE_SAVE,   BTLMENU_PAGE_OPTION, BTLMENU_PAGE_MANUAL, BTLMENU_PAGE_QUIT}
        };
        MenuSelect[0]             = kPages[BtlMenuMode][MenuSelect[1]];
        CommonMenuMes1.stay_frame = false;
        CUserStatus *status       = reinterpret_cast<CUserStatus *>(BtlMenuStatusPt);
        const int    chara        = status->cur_chara;
        u_long128   *buffer       = BtlMenuReadBuf;

        switch (MenuSelect[0]) {
            case BTLMENU_PAGE_CHARA:
                if (BtlMenuMode == BATTLE_MENU_MODE_DUNGEON &&
                    status->res_limit_zone_current >= 0 && status->res_limit_zone_current < 6) {
                    ComMenuSePlay(MENU_SOUND_REFUSE);
                    return 1;
                }
                BtlWakuMake2 = 0;
                Btl_InitMenuChara(buffer);
                break;
            case BTLMENU_PAGE_WEAPON:
                BtlWakuMake2 = 0;
                Btl_InitWeaponSelect(WEP_OPEN_FROM_BAR, chara);
                break;
            case BTLMENU_PAGE_ITEM:
                BtlWakuMake2 = 0;
                Btl_InitItemMode(0, chara);
                break;
            case BTLMENU_PAGE_ATLA: {
                BtlWakuMake2  = 0;
                int blocks[2] = {BtlMenuReadBlock, MenuExtendReadBlock};
                int map       = (BtlMenuMode == BATTLE_MENU_MODE_DUNGEON)
                                ? status->cur_georama
                                : GetNowMapTransAtraMap(MapNo);
                if (map < 0 || map >= 6) map = 0;
                InitMenuAtora1(BtlMenuMode, map, blocks, buffer);
                InitMenuAtoraSelect(map);
                break;
            }
            case BTLMENU_PAGE_MOVE: {
                const int flag = NowGetGameFlagForBtlMenu(BtlMenuMode);
                if (flag == MENU_MOVE_LOCKED) {
                    ComMenuSePlay(MENU_SOUND_REFUSE);
                    return 1;
                }
                InitMenuMove(flag, MenuExtendReadBlock, buffer);
                BtlWakuMake2 = 0;
                break;
            }
            case BTLMENU_PAGE_OPTION:
                BtlWakuMake2 = 0;
                InitMenuOption(OPTION_OPEN_GAME_MENU, MenuExtendReadBlock, buffer);
                break;
            case BTLMENU_PAGE_SAVE:
                BtlWakuMake2 = 0;
                InitMenuSave(SAVE_MENU_MODE_SAVE, MenuExtendReadBlock, buffer);
                break;
            case BTLMENU_PAGE_MANUAL: {
                BtlWakuMake2  = 0;
                int blocks[3] = {MenuExtendReadBlock, BtlMenuExReadBlock, BtlMenuManualReadBlock};
                Btl_BattleManualInit(blocks, buffer);
                break;
            }
            case BTLMENU_PAGE_QUIT:
                s_quit_confirm_active = true;
                s_quit_confirm_choice = 1;
                s_quit_cursor.SnapTo((640 - 100) / 2.0f - 34.0f, (164 + 58 + 10 + 26 + 6) + 3.0f);
                ComMenuSePlay(MENU_SOUND_CONFIRM);
                return 1;
        }

        BattleMenuFlag = MenuSelect[0] + BTLMENU_STATE_OPEN_OFFSET;
        BtlEffectFlag  = BTLEFFECT_PAGE_OPEN;
        BtlEffectCt    = 0;
        ComMenuSePlay(MENU_SOUND_CONFIRM);
    } else if (GamePad.Down(PAD_CIRCLE) != 0) {
        ComMenuSePlay(MENU_SOUND_REFUSE);
        BattleMenuFlag = BTLMENU_STATE_EXIT;
    }

    return 1;
}

PORT_OVERRIDE void DrawMenuMove() {
    if (MenuMove.mode == MENU_MOVE_WORLD_MAP_DIRECT) {
        MenuWorldTrans(&MenuCamera);
        FrameImageDraw(0x40, 0x80);
    }

    MenuCharaFace = TexManager.GetTexture((char*)"charaface", BtlMenuReadBlock);
    BtStatus      = TexManager.GetTexture((char*)"btstatus2", BtlMenuReadBlock);
    WepIcon       = TexManager.GetTexture((char*)"wepicon", -1);
    ItemIcon      = TexManager.GetTexture((char*)"itemicon", BtlMenuReadBlock);
    PerBoardTex   = TexManager.GetTexture((char*)"perbrd", BtlMenuReadBlock);
#ifdef PAL
    WepStatus     = TexManager.GetTexture((char*)"wepstatus", BtlMenuExtBlock);
#else
    WepStatus     = TexManager.GetTexture((char*)"wepstatus", BtlMenuReadBlock);
#endif
    VillageName   = TexManager.GetTexture((char*)"vilname", -1);
    VillageBar    = TexManager.GetTexture((char*)"viltag", -1);

    int alpha = 0x80;
    if (MenuMove.state == MENU_MOVE_STATE_OPEN) {
        alpha = MenuMove.counter * 6;
    } else if (MenuMove.state == MENU_MOVE_STATE_CLOSE) {
        alpha = 0x80 - MenuMove.counter * 8;
    }
    alpha = std::clamp(alpha, 0, 0x80);

    // -------------------------------------------------------------------------
    // 1. Yes/No Travel Confirmation Modals (Divine Beast Cave, Go Outside, Escape)
    //    Rendered using the new UI::ConfirmModal framework
    // -------------------------------------------------------------------------
    if (MenuMove.mode == MENU_MOVE_FIRST_DUNGEON ||
        MenuMove.mode == MENU_MOVE_INTERIOR_OUT  ||
        MenuMove.mode == MENU_MOVE_DUNGEON_ESCAPE) {

        if (MenuMove.mode == MENU_MOVE_DUNGEON_ESCAPE) {
#ifdef PAL
            DrawEscapeItem(0x32, 0x160, alpha);
#else
            DrawEscapeItem(0x32, 0x140, alpha);
#endif
        }

        // Snap the cursor when first opening the Move prompt
        if (s_last_move_state != MenuMove.state && MenuMove.state == MENU_MOVE_STATE_OPEN) {
            const int btn_x  = (640 - 100) / 2;
            const int btn_y0 = 164 + 58 + 10;
            const int sel_y  = (MenuMove.cursor == 0) ? btn_y0 : (btn_y0 + 26 + 6);
            s_move_cursor.SnapTo(static_cast<float>(btn_x - 34), static_cast<float>(sel_y + 3));
        }
        s_last_move_state = MenuMove.state;

        std::string prompt_title, prompt_desc;
        ExtractMovePromptText(prompt_title, prompt_desc);

        UI::ConfirmModal::Draw(
            prompt_title,
            prompt_desc,
            LocalizeText("battlemenu.quit.yes", "YES"),
            LocalizeText("battlemenu.quit.no",  "NO"),
            MenuMove.cursor,
            s_move_cursor,
            alpha
        );

        if (MenuMove.state == MENU_MOVE_STATE_INTERIOR_FADE && GetInteriorOutFlag() != 0) {
            AllFadeForMenu(std::min(0x80, static_cast<int>(MenuMove.counter * 3)));
        }

        if (MenuMove.state == MENU_MOVE_STATE_SELECT || MenuMove.state == MENU_MOVE_STATE_CONFIRM) {
            MenuMove.counter = 0;
        } else {
            MenuMove.counter++;
        }
        return;
    }

    // -------------------------------------------------------------------------
    // 2. World Map Travel Mode (MENU_MOVE_WORLD_MAP / MENU_MOVE_WORLD_MAP_DIRECT)
    // -------------------------------------------------------------------------
    int x = 0;
    int y = 0;
    WORLD_MAP_POS *next = reinterpret_cast<WORLD_MAP_POS *>(NextWorldPos);
    if (next != nullptr) {
        x = next->x;
        y = next->y;
    } else {
        x = 0x150;
        y = 0xB2;
        SysCur[0] = static_cast<float>(x);
        SysCur[1] = static_cast<float>(y);
    }

    const int map_alpha = (MenuMove.state == MENU_MOVE_STATE_CLOSE) ? alpha : 0x80;
    if (MenuMove.ready != 0 && MenuCharaFrame.frame != nullptr) {
        MenuTextureReload(MenuMove.tex_block);
        MenuPolygonDraw(map_alpha, +[]() {
            MenuCharaFrame.SetScale(2.2f, 2.2f, 2.2f);
            MenuCharaFrame.SetPosition(mapo);
            MenuCharaFrame.Step();
            MenuCharaFrame.Draw();
        });
    }

    SysCur[0] += (static_cast<float>(x) - SysCur[0]) / 3.0f;
    SysCur[1] += (static_cast<float>(y) - SysCur[1]) / 3.0f;

    bool arrived = true;
    if (!(std::abs(static_cast<int>(SysCur[0] - static_cast<float>(x))) <= 4.0f) &&
        !(std::abs(static_cast<int>(SysCur[1] - static_cast<float>(y))) <= 4.0f)) {
        arrived = false;
    }
    CommonMenuMes3.stay_frame = false;

    const float width = static_cast<float>(CommonMenuMes3.GetMesWidth_system(CommonMenuMes3.mes_no[0]));
    CommonMenuMes3.text_x = static_cast<int>(static_cast<double>(x) - static_cast<double>(width) / 2.0);
    CommonMenuMes3.text_y = y - 0x22;

    x = static_cast<int>(SysCur[0]);
    y = static_cast<int>(SysCur[1]);

    if (MenuMove.state == MENU_MOVE_STATE_SELECT) {
        MenuTextureReload(MenuMove.tex_block);
        DrawMapCheck(0x80);
#ifdef PAL
        CRect_i_ texel(0xE2, 0, 0x1E, 0x20);
#else
        CRect_i_ texel(0xE0, 0, 0x20, 0x20);
#endif
        DrawObjectVibe(x + 2, y + 2, MenuMoveTex, texel, 0xA, 0x50);
        DrawObjectVibe(x, y, MenuMoveTex, texel, 0x80, 0x80);
        CommonMenuMes3.stay_frame = false;
        MenuHelpWinDraw2(CommonMenuMes3.text_x - 0x18, CommonMenuMes3.text_y - 0xE, 1.0f + width, 0.0f, 0x80, 0, 0, StayTex);
    } else if (MenuMove.state == MENU_MOVE_STATE_CONFIRM) {
        MenuHelpWinDraw2(CommonMenuMes3.text_x - 0x18, CommonMenuMes3.text_y - 0xE, 1.0f + width, 0.0f, 0x80, 0, 0, StayTex);
    }

    MenuTextureReload(CommonMenuMes3.tex_block);
    if (arrived) {
        int mes_alpha = alpha;
        if (MenuMove.state == MENU_MOVE_STATE_ZOOM_IN) {
            mes_alpha = 0x80 - MenuMove.counter * 0xA;
        } else if (MenuMove.state == MENU_MOVE_STATE_ZOOM_OUT) {
            mes_alpha = MenuMove.counter * 6;
        }
        CommonMenuMes3.edge_alpha = std::clamp(mes_alpha, 0, 0x80);
        CommonMenuMes3.Step();
        CommonMenuMes3.DrawMesWin();
    }

    if (MenuMove.state == MENU_MOVE_STATE_CONFIRM) {
        float win_x = 0.0f, win_y = 0.0f, win_w = 0.0f, win_h = 0.0f;
        GetMainMenuRightHelpWinLangOffset(win_x, win_y, win_w, win_h);
        if (GetMenuLangFlag() > LANG_JAPANESE) {
            win_x -= 12.0f;
            win_w += 1.2f;
        }
        MenuHelpWinDraw(static_cast<int>(win_x), static_cast<int>(win_y), win_w, win_h, 0x80);

        int text_x = 0, text_y = 0;
        GetMainMenuRightHelpMsgLangOffset(text_x, text_y);
        CommonMenuMes2.text_x = static_cast<int>(win_x + static_cast<float>(text_x));
        CommonMenuMes2.text_y = static_cast<int>(win_y + static_cast<float>(text_y));
        CommonMenuMes2.Step();
        CommonMenuMes2.DrawMesWin();
    }

    if (MenuMove.mode == MENU_MOVE_WORLD_MAP_DIRECT) {
        MenuTextureReload(MenuMove.tex_block);
        int title_w = 0xD8;
        if (GetMenuLangFlag() > LANG_JAPANESE) {
#ifdef PAL
            title_w = 0xE0;
#else
            title_w = 0xDE;
#endif
        }
        DrawMenu2DSprite(MenuMoveTex, CRect_i_(0x3C, 0x28, title_w, 0x28), CRect_i_(0, 0, title_w, 0x28), 0x80);
    }

    if (MenuMove.state == MENU_MOVE_STATE_SELECT || MenuMove.state == MENU_MOVE_STATE_CONFIRM) {
        MenuMove.counter = 0;
    } else {
        MenuMove.counter++;
    }
}

bool BattleMenuQuitConfirmActive() {
    return s_quit_confirm_active;
}

int* BattleMenuQuitConfirmChoicePtr() {
    return &s_quit_confirm_choice;
}

std::vector<std::pair<std::string, std::string>> UI::BattleMenuStrings() {
    return {
        {"battlemenu.item.title",         "\"Item\""                              },
        {"battlemenu.item.help",          "Use current item or\nSet active items."},
        {"battlemenu.weapon.title",       "\"Weapon\""                            },
        {"battlemenu.weapon.help",        "Change and customise\nweapons."        },
        {"battlemenu.allies.title",       "\"Allies\""                            },
        {"battlemenu.allies.help",        "Change controllable\ncharacter."       },
        {"battlemenu.georama.title",      "\"Georama Parts\""                     },
        {"battlemenu.georama.help",       "Check acquired\ngeorama parts."        },
        {"battlemenu.dungeon.title",      "\"Go to Dungeon\""                     },
        {"battlemenu.dungeon.help",       "Go to Divine\nBeast Cave."             },
        {"battlemenu.world_map.title",    "\"World Map\""                         },
        {"battlemenu.world_map.help",     "Move to another\narea on world map."   },
        {"battlemenu.go_outside.title",   "\"Go Outside\""                        },
        {"battlemenu.go_outside.help",    "Go outside of\nthe building."          },
        {"battlemenu.escape.title",       "\"Escape\""                            },
        {"battlemenu.escape.help",        "Escape from the\ncurrent dungeon."     },
        {"battlemenu.save.title",         "\"Save\""                              },
        {"battlemenu.save.help",          "Record current\ngame progress."        },
        {"battlemenu.option.title",       "\"Options\""                           },
        {"battlemenu.option.help",        "Change game\nconfigurations."          },
        {"battlemenu.journal.title",      "\"Journal\""                           },
        {"battlemenu.journal.help",       "Check manuals and\nchest tracker."     },
        {"battlemenu.quit.title",         "\"Quit Game\""                         },
        {"battlemenu.quit.help",          "Exit the game and\nreturn to desktop." },
        {"battlemenu.quit.confirm_title", "\"Quit Game\""                         },
        {"battlemenu.quit.confirm_desc",  "Return to desktop?"                    },
        {"battlemenu.quit.yes",           "YES"                                   },
        {"battlemenu.quit.no",            "NO"                                    },
    };
}
