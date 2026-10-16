#include "battlemenu/internal.hpp"

namespace {

bool       s_quit_confirm_active = false;
int        s_quit_confirm_choice = 1;
UI::Cursor s_quit_cursor;
char       g_frame_image_name[] = "frame_image";

int GetMenuModeMax() {
    // Keep retail's exact indices for 0..6 (Dungeon) and 0..7 (Town), and append Load & Quit at the end
    // so retail's BattleMenuStep / NorMenuIcon[0..7] state machine never desynchronizes.
    return (BtlMenuMode == BATTLE_MENU_MODE_DUNGEON) ? 9 : 10;
}

void BtlMenuMekeIconInfo(int *icons, int /*menu_mode*/) {
    constexpr s16 kCustomDrawTbl[2][10] = {
        {0, 1, 2, 3, 7, 4, 6, 5, 12,  0}, // Dungeon: Item, Weapon, Allies, Georama, Move, Options, Journal, Load, Quit
        {0, 1, 2, 3, 8, 5, 4, 6,  5, 12}  // Town:    Item, Weapon, Allies, Georama, Move, Save, Options, Journal, Load, Quit
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

void GetMainRingIconTarget(int index, float& out_x, float& out_y) {
    const int count    = GetMenuModeMax();
    const int stride_y = (count >= 10) ? 37 : 40;
    const int start_y  = (count >= 10) ? 46 : 54;

    const int next     = index + 1;
    int       target_x = next * 16 + 80;
    const int target_y = index * stride_y + start_y;
    if (next > 4) {
        target_x -= ((index - 4) << 5) + 16;
    }
    if (target_x < 32) {
        target_x = 32;
    }

    out_x = static_cast<float>(target_x);
    out_y = static_cast<float>(target_y);
}

void GetSafeIconPos(int index, float& out_x, float& out_y) {
    const int  menu_flag    = BattleMenuFlag;
    const bool in_main_ring = (menu_flag == BTLMENU_STATE_MAIN ||
                               menu_flag == BTLMENU_STATE_APPEAR ||
                               menu_flag == BTLMENU_STATE_EXIT);

    if (!in_main_ring) {
        if (index >= 0 && index < 8) {
            out_x = NorMenuIcon[index].x;
            out_y = NorMenuIcon[index].y;
        } else {
            out_x = -200.0f;
            out_y = -200.0f;
        }
        return;
    }

    float base_x = 0.0f, base_y = 0.0f;
    GetMainRingIconTarget(index, base_x, base_y);
    const float slide_dx = NorMenuIcon[0].x - 96.0f;
    out_x = base_x + slide_dx;
    out_y = base_y;
}

struct HelpSpec {
    const char* title_key;
    const char* title_def;
    const char* desc_key;
    const char* desc_def;
};

void GetLocalizedLoadStrings(std::string& out_title, std::string& out_desc) {
    switch (GetMenuLangFlag()) {
        case LANG_FRENCH:
            out_title = "\"Charger\"";
            out_desc  = "Charger une partie\nenregistrée.";
            break;
        case LANG_GERMAN:
            out_title = "\"Laden\"";
            out_desc  = "Gespeicherten Spielstand\nladen.";
            break;
        case LANG_ITALIAN:
            out_title = "\"Carica\"";
            out_desc  = "Carica i dati di\ngioco salvati.";
            break;
        case LANG_SPANISH:
            out_title = "\"Cargar\"";
            out_desc  = "Carga los datos de\nla partida guardada.";
            break;
        default:
            out_title = "\"Load\"";
            out_desc  = "Load saved game\nprogress.";
            break;
    }
}

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
        else if (index == 7) {
            GetLocalizedLoadStrings(out_title, out_desc);
            return;
        }
        else if (index == 8) spec = &kQuitSpec;
    } else {
        if (index == 5)      spec = &kSaveSpec;
        else if (index == 6) spec = &kOptionSpec;
        else if (index == 7) spec = &kJournalSpec;
        else if (index == 8) {
            GetLocalizedLoadStrings(out_title, out_desc);
            return;
        }
        else if (index == 9) spec = &kQuitSpec;
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
    int       icons[10]  = {};
    BtlMenuMekeIconInfo(icons, BtlMenuMode);

    setbilinear(0);
    MenuTextureReload(BtlMenuReadBlock);

    const int  menu_flag    = BattleMenuFlag;
    const bool in_main_ring = (menu_flag == BTLMENU_STATE_MAIN ||
                               menu_flag == BTLMENU_STATE_APPEAR ||
                               menu_flag == BTLMENU_STATE_EXIT);

    if (!in_main_ring &&
        (menu_flag == BTLMENU_STATE_SAVE || menu_flag == BTLMENU_STATE_SAVE_OPEN || menu_flag == BTLMENU_STATE_SAVE_CLOSE) &&
        SaveMenu.access_kind == SAVE_ACCESS_LOAD) {
        return;
    }

    for (int i = 0; i < icon_count; i++) {
        int       icon_alpha = alpha;
        const int brightness = (MenuWarningMsgFlag != 0) ? 64 : 128;

        if (!in_main_ring && i != MenuSelect[1]) {
            continue;
        }

        float icon_x = 0.0f, icon_y = 0.0f;
        GetSafeIconPos(i, icon_x, icon_y);

        if (icon_x < -120.0f) {
            continue;
        }

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
            setbilinear(0);
            MenuTextureReload(BtlMenuReadBlock);
            caption::SetNextCaptionText(GetBattleMenuCaption(i, BtlMenuMode));
            DrawMainMenuIcon(x, y, icons[i], selected, brightness, icon_alpha);
        }
    }
}

void DrawBattleMainOverlay() {
    if (BattleMenuFlag != BTLMENU_STATE_MAIN || s_quit_confirm_active) {
        return;
    }

    int icons[10] = {};
    BtlMenuMekeIconInfo(icons, BtlMenuMode);
    MENU_ICON_INFO *info = GetMenuIconInfo(icons[MenuSelect[1]]);

    UI::Label cap_lbl(GetBattleMenuCaption(MenuSelect[1], BtlMenuMode));
    int       width = cap_lbl.GetWidth() + 0x4A;
    if (info != nullptr && width < (info->label_width + 0x4A)) {
        width = info->label_width + 0x4A;
    }

    float icon_x = 0.0f, icon_y = 0.0f;
    GetSafeIconPos(MenuSelect[1], icon_x, icon_y);

    setbilinear(0);
    MenuTextureReload(BtlMenuReadBlock);
    DrawMenuWaku(icon_x - 18.0f, icon_y - 8.0f, width, 0x22, 0, StayTex, 0x80);
    DrawMenuObjectVibe(static_cast<int>(SysCur[0]), static_cast<int>(SysCur[1]), 1, 0x40);
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

} // namespace

PORT_OVERRIDE void DrawBattleMain() {
    float pos_x = 0.0f, pos_y = 0.0f;
    GetMainRingIconTarget(MenuSelect[1], pos_x, pos_y);

    SysCur[0] += ((pos_x - 44.0f) - SysCur[0]) / 4.0f;
    SysCur[1] += (pos_y - SysCur[1]) / 4.0f;

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
            setbilinear(0);
            DrawCharaSelect();
            setbilinear(0);
            break;
        case BTLMENU_STATE_WEAPON:
        case BTLMENU_STATE_WEAPON_OPEN:
        case BTLMENU_STATE_WEAPON_CLOSE:
            setbilinear(0);
            WeaponMenuDraw();
            setbilinear(0);
            break;
        case BTLMENU_STATE_ITEM:
        case BTLMENU_STATE_ITEM_OPEN:
        case BTLMENU_STATE_ITEM_CLOSE:
            setbilinear(0);
            ItemMenuModeDraw();
            setbilinear(0);
            break;
        case BTLMENU_STATE_ATLA:
        case BTLMENU_STATE_ATLA_OPEN:
        case BTLMENU_STATE_ATLA_CLOSE:
            setbilinear(0);
            DrawMenuAtoraSelect();
            setbilinear(0);
            break;
        case BTLMENU_STATE_MOVE:
        case BTLMENU_STATE_MOVE_OPEN:
        case BTLMENU_STATE_MOVE_CLOSE:
            setbilinear(0);
            DrawMenuMove();
            setbilinear(0);
            break;
        case BTLMENU_STATE_OPTION:
        case BTLMENU_STATE_OPTION_OPEN:
        case BTLMENU_STATE_OPTION_CLOSE:
            setbilinear(0);
            DrawMenuOption();
            setbilinear(0);
            break;
        case BTLMENU_STATE_SAVE:
        case BTLMENU_STATE_SAVE_OPEN:
        case BTLMENU_STATE_SAVE_CLOSE:
            setbilinear(0);
            DrawMenuSave(g_frame_image_name);
            setbilinear(0);
            break;
        case BTLMENU_STATE_MANUAL:
        case BTLMENU_STATE_MANUAL_OPEN:
        case BTLMENU_STATE_MANUAL_CLOSE:
            setbilinear(0);
            MenuManualDraw();
            setbilinear(0);
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

    if (BtlMenuReadEndFlag != 0 && draw_help != 0 &&
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
        DrawBattleMainOverlay();
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

    setbilinear(0);
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
        constexpr int kPages[2][10] = {
            {BTLMENU_PAGE_ITEM, BTLMENU_PAGE_WEAPON, BTLMENU_PAGE_CHARA, BTLMENU_PAGE_ATLA, BTLMENU_PAGE_MOVE,
             BTLMENU_PAGE_OPTION, BTLMENU_PAGE_MANUAL, BTLMENU_PAGE_LOAD, BTLMENU_PAGE_QUIT, BTLMENU_PAGE_NONE},
            {BTLMENU_PAGE_ITEM, BTLMENU_PAGE_WEAPON, BTLMENU_PAGE_CHARA, BTLMENU_PAGE_ATLA, BTLMENU_PAGE_MOVE,
             BTLMENU_PAGE_SAVE, BTLMENU_PAGE_OPTION, BTLMENU_PAGE_MANUAL, BTLMENU_PAGE_LOAD, BTLMENU_PAGE_QUIT}
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
                BtlWakuMake2 = 1;
                Btl_InitWeaponSelect(WEP_OPEN_FROM_BAR, chara);
                break;
            case BTLMENU_PAGE_ITEM:
                BtlWakuMake2 = 1;
                Btl_InitItemMode(0, chara);
                break;
            case BTLMENU_PAGE_ATLA: {
                BtlWakuMake2  = 1;
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
            case BTLMENU_PAGE_LOAD:
                BtlWakuMake2  = 0;
                MenuSelect[0] = BTLMENU_PAGE_SAVE;
                InitMenuSave(SAVE_MENU_MODE_LOAD, MenuExtendReadBlock, buffer);
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

bool BattleMenuQuitConfirmActive() {
    return s_quit_confirm_active;
}

int* BattleMenuQuitConfirmChoicePtr() {
    return &s_quit_confirm_choice;
}

std::vector<std::pair<std::string, std::string>> UI::BattleMenuStrings() {
    return {
        {"battlemenu.item.title",          "\"Item\""                                                 },
        {"battlemenu.item.help",           "Use current item or\nSet active items."                   },
        {"battlemenu.weapon.title",        "\"Weapon\""                                               },
        {"battlemenu.weapon.help",         "Change and customise\nweapons."                           },
        {"battlemenu.allies.title",        "\"Allies\""                                               },
        {"battlemenu.allies.help",         "Change controllable\ncharacter."                          },
        {"battlemenu.georama.title",       "\"Georama Parts\""                                        },
        {"battlemenu.georama.help",        "Check acquired\ngeorama parts."                           },
        {"battlemenu.dungeon.title",       "\"Go to Dungeon\""                                        },
        {"battlemenu.dungeon.help",        "Go to Divine\nBeast Cave."                                },
        {"battlemenu.world_map.title",     "\"World Map\""                                            },
        {"battlemenu.world_map.help",      "Move to another\narea on world map."                      },
        {"battlemenu.go_outside.title",    "\"Go Outside\""                                           },
        {"battlemenu.go_outside.help",     "Go outside of\nthe building."                             },
        {"battlemenu.escape.title",        "\"Escape\""                                               },
        {"battlemenu.escape.help",         "Escape from the\ncurrent dungeon."                        },
        {"battlemenu.save.title",          "\"Save\""                                                 },
        {"battlemenu.save.help",           "Record current\ngame progress."                           },
        {"battlemenu.option.title",        "\"Options\""                                              },
        {"battlemenu.option.help",         "Change game\nconfigurations."                             },
        {"battlemenu.journal.title",       "\"Journal\""                                              },
        {"battlemenu.journal.help",        "Check manuals and\nchest tracker."                        },
        {"battlemenu.quit.title",          "\"Quit Game\""                                            },
        {"battlemenu.quit.help",           "Exit the game and\nreturn to desktop."                    },
        {"battlemenu.quit.confirm_title",  "\"Quit Game\""                                            },
        {"battlemenu.quit.confirm_desc",   "Return to desktop?"                                       },
        {"battlemenu.quit.yes",            "YES"                                                      },
        {"battlemenu.quit.no",             "NO"                                                       },
        {"weaponmenu.action.equip",        "EQUIP"                                                    },
        {"weaponmenu.action.customize",    "CUSTOMIZE"                                                },
        {"weaponmenu.action.attribute",    "ATTRIBUTE"                                                },
        {"weaponmenu.action.repair",       "REPAIR"                                                   },
        {"weaponmenu.action.upgrade",      "UPGRADE"                                                  },
        {"weaponmenu.action.synthesize",   "SYNTHESIZE"                                               },
        {"weaponmenu.action.buildup",      "BUILD-UP"                                                 },
        {"weaponmenu.action.discard",      "DISCARD"                                                  },
        {"weaponmenu.repair_powder",       "Repair Powder"                                            },
        {"weaponmenu.powerup_powder",      "Powerup Powder"                                           },
        {"weaponmenu.upgrade.confirm",     "Upgrade. Confirm?"                                        },
        {"weaponmenu.buildup.confirm",     "Build-up. Confirm?"                                       },
        {"weaponmenu.synthesize.confirm",  "Status break this weapon?\nWeapon disappears."            },
        {"weaponmenu.discard.confirm",     "Throw away this weapon?"                                  },
        {"weaponmenu.buildup.prompt",      "Select weapon to build up to:"                            },
    };
}