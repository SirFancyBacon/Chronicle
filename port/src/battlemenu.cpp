#include "battlemenu.hpp"

// Port-specific headers
#include <SDL3/SDL.h>

// Retail headers
#include "btactstatus.hpp"
#include "camera.hpp"
#include "character.hpp"
#include "clsmes.hpp"
#include "dataread.hpp"
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "eastking.hpp"
#include "gamepad.hpp"
#include "itemdata.hpp"
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

#include <string>
#include <vector>

// Externs for hijacked static functions
extern void Btl_InitMenuChara(u_long128 *buffer);
extern void Btl_InitWeaponSelect(int mode, int chara);
extern void Btl_InitItemMode(int mode, int chara);
extern void Btl_BattleManualInit(int*, u_long128*);

#define BTLMENU_PAGE_QUIT 100

namespace {

int GetMenuModeMax() {
    int icon_count = 0;
    switch (BtlMenuMode) {
        case BATTLE_MENU_MODE_DUNGEON:
            icon_count = 8;
            break;
        case BATTLE_MENU_MODE_TOWN:
            icon_count = 9;
            break;
    }
    return icon_count;
}

void BtlMenuMekeIconInfo(int *icons, int menu_mode) {
    int icon_count = GetMenuModeMax();

    s16 custom_draw_tbl[2][9] = {
        {0, 1, 2, 3, 7, 4, 6, 12, 0}, // Dungeon
        {0, 1, 2, 3, 8, 5, 4, 6, 12}  // Town
    };

    for (int i = 0; i < icon_count; i++) {
        icons[i] = custom_draw_tbl[BtlMenuMode][i];
    }

    if (BtlMenuMode != BATTLE_MENU_MODE_DUNGEON) {
        int special_icon = 0;
        switch (NowGetGameFlagForBtlMenu(BtlMenuMode)) {
            case MENU_MOVE_LOCKED:
            case MENU_MOVE_FIRST_DUNGEON: special_icon = 9; break;
            case MENU_MOVE_WORLD_MAP:     special_icon = 8; break;
            case MENU_MOVE_INTERIOR_OUT:  special_icon = 0xC; break;
        }
        icons[4] = special_icon;
    }
}

void GetSafeIconPos(int index, float& out_x, float& out_y) {
    int orig_max = (BtlMenuMode == BATTLE_MENU_MODE_DUNGEON) ? 7 : 8;
    if (index < orig_max) {
        out_x = NorMenuIcon[index].x;
        out_y = NorMenuIcon[index].y;
    } else {
        int diff = index - (orig_max - 1);
        out_x = NorMenuIcon[orig_max - 1].x - (16.0f * diff);
        out_y = NorMenuIcon[orig_max - 1].y + (40.0f * diff);
    }
}

const char* GetBattleMenuLabel(int index, int mode) {
    if (mode == BATTLE_MENU_MODE_DUNGEON) {
        switch (index) {
            case 0: return "ITEM";
            case 1: return "WEAPON";
            case 2: return "ALLIES";
            case 3: return "GEORAMA PARTS";
            case 4: {
                int escape_flag = GetEscapeDngFlag();
                return (escape_flag == 2) ? "ESCAPE" : "GO OUTSIDE";
            }
            case 5: return "OPTION";
            case 6: return "JOURNAL";
            case 7: return "QUIT GAME";
            default: return "";
        }
    } else {
        switch (index) {
            case 0: return "ITEM";
            case 1: return "WEAPON";
            case 2: return "ALLIES";
            case 3: return "GEORAMA PARTS";
            case 4: {
                switch (NowGetGameFlagForBtlMenu(BtlMenuMode)) {
                    case MENU_MOVE_WORLD_MAP:     return "WORLD MAP";
                    case MENU_MOVE_INTERIOR_OUT:  return "GO OUTSIDE";
                    default:                      return "GO TO DUNGEON";
                }
            }
            case 5: return "SAVE";
            case 6: return "OPTION";
            case 7: return "JOURNAL";
            case 8: return "QUIT GAME";
            default: return "";
        }
    }
}

void GetBattleMenuHelp(int index, int mode, const char*& out_title, const char*& out_desc) {
    if (mode == BATTLE_MENU_MODE_DUNGEON) {
        switch (index) {
            case 0: out_title = "Item";         out_desc = "Use, check, or discard items."; break;
            case 1: out_title = "Weapon";       out_desc = "Equip, repair, or upgrade weapons."; break;
            case 2: out_title = "Allies";       out_desc = "Change active party member."; break;
            case 3: out_title = "Georama";      out_desc = "View collected town parts."; break;
            case 4: out_title = "Escape";       out_desc = "Escape from the current dungeon."; break;
            case 5: out_title = "Option";       out_desc = "Adjust game settings."; break;
            case 6: out_title = "Journal";      out_desc = "Chest Tracker & manuals."; break;
            case 7: out_title = "Quit Game";    out_desc = "Exit Chronicle to desktop."; break;
            default: out_title = ""; out_desc = ""; break;
        }
    } else {
        switch (index) {
            case 0: out_title = "Item";         out_desc = "Use, check, or discard items."; break;
            case 1: out_title = "Weapon";       out_desc = "Equip, repair, or upgrade weapons."; break;
            case 2: out_title = "Allies";       out_desc = "Change active party member."; break;
            case 3: out_title = "Georama";      out_desc = "Assemble and view town pieces."; break;
            case 4: out_title = "Move";         out_desc = "Travel to dungeon or world locations."; break;
            case 5: out_title = "Save";         out_desc = "Save current game progress."; break;
            case 6: out_title = "Option";       out_desc = "Adjust game settings."; break;
            case 7: out_title = "Journal";      out_desc = "Chest Tracker & manuals."; break;
            case 8: out_title = "Quit Game";    out_desc = "Exit Chronicle to desktop."; break;
            default: out_title = ""; out_desc = ""; break;
        }
    }
}

void DrawBtlMenuBar() {
    int alpha;
    int icon_alpha;
    int i;
    int x;
    int menu_flag;
    int brightness;
    int selected;
    int y;

    if (BtlEffectFlag == BTLEFFECT_PAGE_CLOSE || BattleMenuFlag == BTLMENU_STATE_APPEAR) {
        alpha = 10.0f * BtlEffectCt;
        if (alpha >= 128) alpha = 128;
    }

    if (BtlEffectFlag == BTLEFFECT_PAGE_OPEN || BattleMenuFlag == BTLMENU_STATE_EXIT) {
        alpha = 128.0f - 10.0f * BtlEffectCt;
        if (alpha < 0) alpha = 0;
    } else {
        alpha = 128;
    }

    NowGetGameFlagForBtlMenu(BtlMenuMode);
    int icon_count = GetMenuModeMax();
    int icons[9] = {};
    BtlMenuMekeIconInfo(icons, BtlMenuMode);

    // 1. Suppress baked texture labels
    for (i = 0; i < icon_count; i++) {
        if (icons[i] >= 0) {
            MENU_ICON_INFO* info = GetMenuIconInfo(icons[i]);
            if (info != nullptr) {
                info->label_width = 0;
            }
        }
    }

    // 2. Draw circular medallion icons
    for (i = 0; i < icon_count; i++) {
        icon_alpha = alpha;
        brightness = 128;

        if (MenuWarningMsgFlag != 0) brightness = 64;

        menu_flag = BattleMenuFlag;

        if (menu_flag != BTLMENU_STATE_MAIN && menu_flag != BTLMENU_STATE_APPEAR && menu_flag != BTLMENU_STATE_EXIT) {
            if (i != MenuSelect[1]) {
                continue;
            }
        }

        selected = 0;
        
        float icon_x, icon_y;
        GetSafeIconPos(i, icon_x, icon_y);

        x = icon_x;
        float lowered_y = 2.0f + icon_y;
        y = 1.0f + lowered_y;

        if (i == MenuSelect[1]) {
            if ((menu_flag == BTLMENU_STATE_APPEAR || menu_flag == BTLMENU_STATE_EXIT) &&
                menu_flag == BTLMENU_STATE_MOVE_OPEN) {
                icon_alpha = alpha;
            }
            selected = 1;
            x = icon_x - 10.0f;
            y = 1.0f + (icon_y - 2.0f);
            icon_alpha = 128;
        }

        if (icons[i] >= 0) {
            DrawMainMenuIcon(x, y, icons[i], selected, brightness, icon_alpha);
        }
    }

    // 3. Render all-caps text centered vertically with medallion icons
    MenuTextureReload(0x1A);
    for (i = 0; i < icon_count; i++) {
        menu_flag = BattleMenuFlag;
        if (menu_flag != BTLMENU_STATE_MAIN && menu_flag != BTLMENU_STATE_APPEAR && menu_flag != BTLMENU_STATE_EXIT) {
            if (i != MenuSelect[1]) {
                continue;
            }
        }

        float icon_x, icon_y;
        GetSafeIconPos(i, icon_x, icon_y);

        float label_x = icon_x + 72.0f;
        float label_y = icon_y + 11.0f; // Centered with icon center (icon_y + 19)

        if (i == MenuSelect[1]) {
            label_x -= 10.0f;
            label_y -= 4.0f; // Centered with selected icon center (icon_y + 15)
        }

        const char* label_str = GetBattleMenuLabel(i, BtlMenuMode);
        UI::Label lbl(label_str);
        lbl.DrawWithStroke(static_cast<int>(label_x), static_cast<int>(label_y), UI::ColorWhite, UI::ColorBlue, 2, alpha);
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

char g_frame_image_name[] = "frame_image";

} // namespace

PORT_OVERRIDE void DrawBattleMain() {
    int pos[2];
    int icons[9];

    int next = MenuSelect[1] + 1;
    pos[0] = next * 16 + 80;
    pos[1] = MenuSelect[1] * 40 + 54;
    if (next > 4) {
        pos[0] -= ((MenuSelect[1] - 4) << 5) + 16;
    }

    int x = pos[0] - 0x2C;
    int y = pos[1];
    SysCur[0] += ((float) x - SysCur[0]) / 4.0f;
    SysCur[1] += ((float) y - SysCur[1]) / 4.0f;
    
    BtlMenuMekeIconInfo(icons, BtlMenuMode);

    if (BtlMenuMode == BATTLE_MENU_MODE_DUNGEON) {
        int limit = GetLimmitMsg();
        if (CommonMenuMes1.mes_made != limit) {
            CommonMenuMes1.MakeMesWin(limit);
        }
        if (limit >= 0) {
            MenuTextureReload(CommonMenuMes1.tex_block);
            CommonMenuMes1.text_x = 0x50;
            CommonMenuMes1.text_y = 0x14A;
            CommonMenuMes1.stay_frame = true;
            CommonMenuMes1.Step();
            CommonMenuMes1.DrawMesWin();
        }
    }

    // Dynamic bracket width adjusted for the +72px label offset
    MenuTextureReload(0x1A);
    const char* label_str = GetBattleMenuLabel(MenuSelect[1], BtlMenuMode);
    UI::Label lbl(label_str);
    int width = lbl.GetWidth() + 98;

    float icon_x, icon_y;
    GetSafeIconPos(MenuSelect[1], icon_x, icon_y);

    float waku_y = icon_y - 8.0f;
    DrawMenuWaku(icon_x - 18.0f, waku_y, width, 0x22, 0, StayTex, 0x80);
    DrawMenuObjectVibe((int) SysCur[0], (int) SysCur[1], 1, 0x40);
}

PORT_OVERRIDE void BattleMenuDraw() {
    MenuMouseNoteBattleMenu();

    setbilinear(0);
    MenuWorldTrans(&MenuCamera);
    MenuTextureReload(BtlMenuReadBlock);
    CTexture  frame = *TexManager.GetTexture(g_frame_image_name, -1);
    CTexture *texture = &frame;
    int       tcc = 0;

    if (texture != NULL) {
        ((sceGsTex0 *) &frame.tex0)->bits.tcc = tcc;
    } else {
        return;
    }

    int bright = 0x40;

    switch (BattleMenuFlag) {
        case BTLMENU_STATE_APPEAR:
            bright = (int) (128.0f - 4.0f * BtlEffectCt);
            if (bright < 0x40) bright = 0x40;
            break;
        case BTLMENU_STATE_EXIT:
            bright += 4.0f * BtlEffectCt;
            if (bright > 0x80) bright = 0x80;
            break;
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

    CursorVibeCnt++;

    if (CursorVibeCnt >= 0x405F7E00) {
        CursorVibeCnt = 0;
    }

    int draw_help = BtlMenuDrawSpecialFlag(BtlWakuMake2);

    if (BtlMenuReadEndFlag != 0) {
        switch (BattleMenuFlag) {
            case BTLMENU_STATE_EXIT:
                BtlHelpWinAlpha -= 8;
                if (BtlHelpWinAlpha < 0) BtlHelpWinAlpha = 0;
                break;
            default:
                BtlHelpWinAlpha += 9;
                if (BtlHelpWinAlpha > 0x80) BtlHelpWinAlpha = 0x80;
                break;
        }
    }

    // Dynamic text-wrapped help box anchored at bottom right
    if (BtlMenuReadEndFlag != 0 && draw_help != 0 && BtlHelpWinAlpha > 0) {
        const int box_w = 280;
        const int box_right_margin = 18;
        const int box_x = 640 - box_w - box_right_margin;
        const int box_bottom = 452;

        const char* title = "";
        const char* desc  = "";
        GetBattleMenuHelp(MenuSelect[1], BtlMenuMode, title, desc);

        // Word-wrap description text to fit inner width (280 - 48 = 232)
        const int max_text_w = box_w - 48;
        std::vector<std::string> desc_lines;

        MenuTextureReload(0x1A);
        std::string current_line;
        const char* p = desc;

        while (*p) {
            while (*p == ' ') p++;
            if (*p == '\0') break;

            const char* word_start = p;
            while (*p && *p != ' ') p++;
            std::string word(word_start, p - word_start);

            std::string candidate = current_line.empty() ? word : (current_line + " " + word);
            UI::Label test_lbl(candidate);
            if (test_lbl.GetWidth() > max_text_w && !current_line.empty()) {
                desc_lines.push_back(current_line);
                current_line = word;
            } else {
                current_line = candidate;
            }
        }
        if (!current_line.empty()) {
            desc_lines.push_back(current_line);
        }

        // Dynamic height: 20 top padding + 20 title + 6 gap + (lines * 20) + 20 bottom padding
        const int line_h = 20;
        const int top_padding = 20;
        const int title_h = 20;
        const int title_desc_gap = 6;
        const int desc_h = static_cast<int>(desc_lines.size()) * line_h;
        const int bottom_padding = 20;

        int box_h = top_padding + title_h + title_desc_gap + desc_h + bottom_padding;
        if (box_h < 76) box_h = 76;

        int box_y = box_bottom - box_h;

        // Draw dynamic window plate
        UI::Window::Draw(box_x, box_y, box_w, box_h, BtlHelpWinAlpha);
        MenuTextureReload(0x1A);

        // Draw title
        UI::Label title_lbl(title, UI::ColorYellow);
        title_lbl.Draw(box_x + 24, box_y + top_padding, BtlHelpWinAlpha);

        // Draw wrapped description lines
        int desc_y = box_y + top_padding + title_h + title_desc_gap;
        for (size_t i = 0; i < desc_lines.size(); ++i) {
            UI::Label line_lbl(desc_lines[i], UI::ColorWhite);
            line_lbl.Draw(box_x + 24, desc_y + (static_cast<int>(i) * line_h), BtlHelpWinAlpha);
        }
    }

    setbilinear(0);
    draw_help = BtlMenuDrawSpecialFlag(1);

    if (draw_help != 0) {
        DrawBtlMenuBar();
    }

    if (WepMenu.mode != WEP_MENU_BUILDUP_SELECT) {
        MenuWepLevelUp.Step();
        MenuWepLevelUp.Draw();
    }

    MenuMouseDrawOverlay();

    if (GetInteriorOutFlag() != 0) {
        BtlEffectCt += 1.0f;
        int fade = (int) (3.0f * BtlEffectCt);
        if (fade > 0x80) fade = 0x80;
        AllFadeForMenu(fade);
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

    int old = MenuSelect[1];
    int max = GetMenuModeMax();

    if (GamePad.Down(PAD_UP | PAD_LEFT) != 0) {
        MenuSelect[1]--;
        if (MenuSelect[1] < 0) {
            MenuSelect[1] = max - 1;
        }
    }

    if (GamePad.Down(PAD_RIGHT | PAD_DOWN) != 0) {
        MenuSelect[1]++;
        if (MenuSelect[1] > max - 1) {
            MenuSelect[1] = 0;
        }
    }

    if (old != MenuSelect[1]) {
        ComMenuSePlay(MENU_SOUND_CURSOR);
    }

    if (GamePad.Down(PAD_CROSS) != 0) {
        int pages[2][9] = {
            {BTLMENU_PAGE_ITEM, BTLMENU_PAGE_WEAPON, BTLMENU_PAGE_CHARA, BTLMENU_PAGE_ATLA, BTLMENU_PAGE_MOVE, BTLMENU_PAGE_OPTION, BTLMENU_PAGE_MANUAL, BTLMENU_PAGE_QUIT, BTLMENU_PAGE_NONE},
            {BTLMENU_PAGE_ITEM, BTLMENU_PAGE_WEAPON, BTLMENU_PAGE_CHARA, BTLMENU_PAGE_ATLA, BTLMENU_PAGE_MOVE, BTLMENU_PAGE_SAVE,   BTLMENU_PAGE_OPTION, BTLMENU_PAGE_MANUAL, BTLMENU_PAGE_QUIT}
        };
        MenuSelect[0] = pages[BtlMenuMode][MenuSelect[1]];
        CommonMenuMes1.stay_frame = false;
        CUserStatus *status = (CUserStatus *) BtlMenuStatusPt;
        int          chara = status->cur_chara;
        u_long128   *buffer = BtlMenuReadBuf;

        switch (MenuSelect[0]) {
            case BTLMENU_PAGE_CHARA:
                if (BtlMenuMode == BATTLE_MENU_MODE_DUNGEON) {
                    int zone = status->res_limit_zone_current;
                    if (zone < 6 && zone >= 0) {
                        ComMenuSePlay(MENU_SOUND_REFUSE);
                        return 1;
                    }
                }
                Btl_InitMenuChara(buffer);
                break;
            case BTLMENU_PAGE_WEAPON:
                Btl_InitWeaponSelect(WEP_OPEN_FROM_BAR, chara);
                break;
            case BTLMENU_PAGE_ITEM:
                Btl_InitItemMode(0, chara);
                break;
            case BTLMENU_PAGE_ATLA: {
                int blocks[2] = {0, 0};
                blocks[0] = BtlMenuReadBlock;
                blocks[1] = MenuExtendReadBlock;
                int map = 0;
                switch (BtlMenuMode) {
                    case BATTLE_MENU_MODE_DUNGEON:
                        map = status->cur_georama;
                        if (map < 0) map = 0;
                        if (map >= 6) map = 0;
                        break;
                    case BATTLE_MENU_MODE_TOWN:
                        map = GetNowMapTransAtraMap(MapNo);
                        if (map < 0 || map > 5) map = 0;
                        break;
                }
                InitMenuAtora1(BtlMenuMode, map, blocks, buffer);
                InitMenuAtoraSelect(map);
                break;
            }
            case BTLMENU_PAGE_MOVE: {
                int flag = NowGetGameFlagForBtlMenu(BtlMenuMode);
                switch (flag) {
                    case MENU_MOVE_LOCKED:
                        ComMenuSePlay(MENU_SOUND_REFUSE);
                        return 1;
                    default:
                    case MENU_MOVE_WORLD_MAP:
                    case MENU_MOVE_FIRST_DUNGEON:
                    case MENU_MOVE_INTERIOR_OUT:
                    case MENU_MOVE_DUNGEON_ESCAPE:
                        InitMenuMove(flag, MenuExtendReadBlock, buffer);
                        BtlWakuMake2 = 0;
                        break;
                }
                break;
            }
            case BTLMENU_PAGE_OPTION:
                InitMenuOption(OPTION_OPEN_GAME_MENU, MenuExtendReadBlock, buffer);
                break;
            case BTLMENU_PAGE_SAVE:
                BtlWakuMake2 = 0;
                InitMenuSave(SAVE_MENU_MODE_SAVE, MenuExtendReadBlock, buffer);
                break;
            case BTLMENU_PAGE_MANUAL: {
                BtlWakuMake2 = 0;
                int blocks[3] = {0, 0, 0};
                blocks[0] = MenuExtendReadBlock;
                blocks[1] = BtlMenuExReadBlock;
                blocks[2] = BtlMenuManualReadBlock;
                extern void Btl_BattleManualInit(int*, u_long128*); 
                Btl_BattleManualInit(blocks, buffer);
                break;
            }
            case BTLMENU_PAGE_QUIT: {
                SDL_Event event{};
                event.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&event);
                return 1;
            }
        }

        if (MenuSelect[0] != BTLMENU_PAGE_QUIT) {
            BattleMenuFlag = MenuSelect[0] + BTLMENU_STATE_OPEN_OFFSET;
            BtlEffectFlag = BTLEFFECT_PAGE_OPEN;
            BtlEffectCt = 0;
            ComMenuSePlay(MENU_SOUND_CONFIRM);
        }
    } else if (GamePad.Down(PAD_CIRCLE) != 0) {
        ComMenuSePlay(MENU_SOUND_REFUSE);
        BattleMenuFlag = BTLMENU_STATE_EXIT;
    }

    return 1;
}