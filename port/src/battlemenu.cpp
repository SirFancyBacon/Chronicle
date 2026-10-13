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

#include <string>
#include <vector>

extern void Btl_InitMenuChara(u_long128 *buffer);
extern void Btl_InitWeaponSelect(int mode, int chara);
extern void Btl_InitItemMode(int mode, int chara);
extern void Btl_BattleManualInit(int*, u_long128*);

#define BTLMENU_PAGE_QUIT 100

namespace {

bool       s_quit_confirm_active = false;
int        s_quit_confirm_choice = 1; // 0 = YES, 1 = NO (default to NO)
UI::Cursor s_quit_cursor;

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

void GetBattleMenuHelpData(int index, int mode, std::string& out_title, std::string& out_desc) {
    if (mode == BATTLE_MENU_MODE_DUNGEON) {
        switch (index) {
            case 0:
                out_title = LocalizeText("battlemenu.item.title", "\"Item\"");
                out_desc  = LocalizeText("battlemenu.item.help", "Use current item or\nSet active items.");
                break;
            case 1:
                out_title = LocalizeText("battlemenu.weapon.title", "\"Weapon\"");
                out_desc  = LocalizeText("battlemenu.weapon.help", "Change and customise\nweapons.");
                break;
            case 2:
                out_title = LocalizeText("battlemenu.allies.title", "\"Allies\"");
                out_desc  = LocalizeText("battlemenu.allies.help", "Change controllable\ncharacter.");
                break;
            case 3:
                out_title = LocalizeText("battlemenu.georama.title", "\"GeoramaParts\"");
                out_desc  = LocalizeText("battlemenu.georama.help", "Check acquired\ngeorama parts.");
                break;
            case 4: {
                int escape_flag = GetEscapeDngFlag();
                if (escape_flag == 2) {
                    out_title = LocalizeText("battlemenu.escape.title", "\"Escape\"");
                    out_desc  = LocalizeText("battlemenu.escape.help", "Escape from the\ncurrent dungeon.");
                } else {
                    out_title = LocalizeText("battlemenu.go_outside.title", "\"Go Outside\"");
                    out_desc  = LocalizeText("battlemenu.go_outside.help", "Go outside of\nthe building.");
                }
                break;
            }
            case 5:
                out_title = LocalizeText("battlemenu.option.title", "\"Options\"");
                out_desc  = LocalizeText("battlemenu.option.help", "Change game\nconfigurations.");
                break;
            case 6:
                out_title = LocalizeText("battlemenu.journal.title", "\"Journal\"");
                out_desc  = LocalizeText("battlemenu.journal.help", "Check manuals and\nchest tracker.");
                break;
            case 7:
                out_title = LocalizeText("battlemenu.quit.title", "\"Quit Game\"");
                out_desc  = LocalizeText("battlemenu.quit.help", "Exit the game and\nreturn to desktop.");
                break;
            default:
                out_title.clear();
                out_desc.clear();
                break;
        }
    } else {
        switch (index) {
            case 0:
                out_title = LocalizeText("battlemenu.item.title", "\"Item\"");
                out_desc  = LocalizeText("battlemenu.item.help", "Use current item or\nSet active items.");
                break;
            case 1:
                out_title = LocalizeText("battlemenu.weapon.title", "\"Weapon\"");
                out_desc  = LocalizeText("battlemenu.weapon.help", "Change and customise\nweapons.");
                break;
            case 2:
                out_title = LocalizeText("battlemenu.allies.title", "\"Allies\"");
                out_desc  = LocalizeText("battlemenu.allies.help", "Change controllable\ncharacter.");
                break;
            case 3:
                out_title = LocalizeText("battlemenu.georama.title", "\"GeoramaParts\"");
                out_desc  = LocalizeText("battlemenu.georama.help", "Check acquired\ngeorama parts.");
                break;
            case 4: {
                int flag = NowGetGameFlagForBtlMenu(BtlMenuMode);
                switch (flag) {
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
                break;
            }
            case 5:
                out_title = LocalizeText("battlemenu.save.title", "\"Save\"");
                out_desc  = LocalizeText("battlemenu.save.help", "Record current\ngame progress.");
                break;
            case 6:
                out_title = LocalizeText("battlemenu.option.title", "\"Options\"");
                out_desc  = LocalizeText("battlemenu.option.help", "Change game\nconfigurations.");
                break;
            case 7:
                out_title = LocalizeText("battlemenu.journal.title", "\"Journal\"");
                out_desc  = LocalizeText("battlemenu.journal.help", "Check manuals and\nchest tracker.");
                break;
            case 8:
                out_title = LocalizeText("battlemenu.quit.title", "\"Quit Game\"");
                out_desc  = LocalizeText("battlemenu.quit.help", "Exit the game and\nreturn to desktop.");
                break;
            default:
                out_title.clear();
                out_desc.clear();
                break;
        }
    }
}

void DrawQuitConfirmModal(int alpha) {
    const int box_w = 260;
    const int box_h = 58;
    const int box_x = (640 - box_w) / 2;
    const int box_y = 164;

    UI::HelpBox::Draw(box_x, box_y, box_w, box_h, alpha);

    MenuTextureReload(0x1A);
    std::string q_title = LocalizeText("battlemenu.quit.confirm_title", "\"Quit Game\"");
    std::string q_desc  = LocalizeText("battlemenu.quit.confirm_desc", "Return to desktop?");

    UI::Label title_lbl(q_title, UI::ColorWhite);
    title_lbl.Draw(box_x + 22, box_y + 12, alpha);

    UI::Label desc_lbl(q_desc, UI::ColorWhite);
    desc_lbl.Draw(box_x + 22, box_y + 32, alpha);

    // Narrowed button proportions (120px x 28px)
    const int btn_w  = 120;
    const int btn_h  = 28;
    const int btn_x  = (640 - btn_w) / 2;
    const int btn_y0 = box_y + box_h + 10; // 232
    const int btn_y1 = btn_y0 + btn_h + 6;  // 266

    CTexture* gaiji_tex = draw2d::Get().find_texture("gaiji");
    if (gaiji_tex == nullptr) {
        gaiji_tex = TexManager.GetTexture((char*)"gaiji", -1);
    }

    auto DrawBevelButton = [&](int bx, int by, const std::string& text) {
        if (gaiji_tex != nullptr) {
            CRect_i_ uv_white(0x6A, 0x6E, 1, 1);

            DrawMenu2DSprite(gaiji_tex, CRect_i_(bx + 2, by + 3, btn_w, btn_h), uv_white, 0, 0, 0, (alpha * 90) >> 7);
            DrawMenu2DSprite(gaiji_tex, CRect_i_(bx, by, btn_w, btn_h), uv_white, 20, 10, 5, alpha);
            DrawMenu2DSprite(gaiji_tex, CRect_i_(bx + 1, by + 1, btn_w - 2, btn_h - 2), uv_white, 115, 75, 50, alpha);
            DrawMenu2DSprite(gaiji_tex, CRect_i_(bx + 2, by + 2, btn_w - 3, btn_h - 3), uv_white, 55, 28, 14, alpha);
            DrawMenu2DSprite(gaiji_tex, CRect_i_(bx + 2, by + 2, btn_w - 4, btn_h - 4), uv_white, 100, 56, 33, alpha);
        }

        MenuTextureReload(0x1A);
        UI::Label lbl(text, UI::ColorWhite, UI::Alignment::Center);
        // Vertically centered at by + 3
        lbl.Draw(bx + (btn_w / 2), by + 3, alpha);
    };

    std::string txt_yes = LocalizeText("battlemenu.quit.yes", "YES");
    std::string txt_no  = LocalizeText("battlemenu.quit.no", "NO");

    DrawBevelButton(btn_x, btn_y0, txt_yes);
    DrawBevelButton(btn_x, btn_y1, txt_no);

    int sel_y = (s_quit_confirm_choice == 0) ? btn_y0 : btn_y1;

    // Symmetrical 3px bracket padding around button perimeter
    constexpr int pad = 3;
    DrawMenuWaku(btn_x - pad, sel_y - pad, btn_w + 2 * pad - 16, btn_h + 2 * pad - 16, 0, StayTex, alpha);

    // Hand cursor centered on the active button
    s_quit_cursor.SetTarget(static_cast<float>(btn_x - 34), static_cast<float>(sel_y + 4));
    s_quit_cursor.Draw(alpha);
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
    MENU_ICON_INFO *info = GetMenuIconInfo(icons[MenuSelect[1]]);

    BtlWakuMake2 = 1;
    CommonMenuMes2.mes_made = -1;
    CommonMenuMes2.stay_frame = false;

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

    if (!s_quit_confirm_active) {
        int width = (info != nullptr) ? (info->label_width + 0x4A) : 100;

        float icon_x, icon_y;
        GetSafeIconPos(MenuSelect[1], icon_x, icon_y);

        float waku_y = icon_y - 8.0f;
        DrawMenuWaku(icon_x - 18.0f, waku_y, width, 0x22, 0, StayTex, 0x80);
        DrawMenuObjectVibe((int) SysCur[0], (int) SysCur[1], 1, 0x40);
    }
}

PORT_OVERRIDE void BattleMenuDraw() {
    MenuMouseNoteBattleMenu();
    int text_x = 0;
    int text_y = 0;

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
                if (BattleMenuFlag == BTLMENU_STATE_MAIN || BattleMenuFlag == BTLMENU_STATE_APPEAR) {
                    BtlHelpWinAlpha += 9;
                    if (BtlHelpWinAlpha > 0x80) BtlHelpWinAlpha = 0x80;
                } else {
                    BtlHelpWinAlpha -= 8;
                    if (BtlHelpWinAlpha < 0) BtlHelpWinAlpha = 0;
                }
                break;
        }
    }

    GetMainMenuRightHelpMsgLangOffset(text_x, text_y);

    // Dynamic help box sizing and line height
    // 28px line pitch between description lines
    if (BtlMenuReadEndFlag != 0 && draw_help != 0 && BattleMenuFlag == BTLMENU_STATE_MAIN && BtlHelpWinAlpha > 0 && !s_quit_confirm_active) {
        std::string title;
        std::string desc;
        GetBattleMenuHelpData(MenuSelect[1], BtlMenuMode, title, desc);

        if (!title.empty() && !desc.empty()) {
            MenuTextureReload(0x1A);

            const int box_w = 276;
            const int box_x = (HelpWinHead[0] > 0) ? static_cast<int>(HelpWinHead[0]) : (640 - box_w - 20);
            const int draw_x = box_x + (text_x > 0 ? text_x : 22);

            const int max_text_w = 230;
            constexpr int line_height = 28; // Expanded line pitch for wrapped TTF lines
            std::vector<std::string> lines = UI::WrapText(desc, max_text_w);
            int num_lines = std::max(1, static_cast<int>(lines.size()));

            // Dynamic height: 32px base + (num_lines * 28px)
            int box_h = 32 + (num_lines * line_height);
            const int box_bottom = 452;
            int box_y = box_bottom - box_h;

            UI::HelpBox::Draw(box_x, box_y, box_w, box_h, BtlHelpWinAlpha);

            MenuTextureReload(0x1A);
            int title_y = box_y + 12;
            int desc_y  = title_y + 24;

            UI::Label title_lbl(title, UI::ColorWhite);
            title_lbl.Draw(draw_x, title_y, BtlHelpWinAlpha);

            for (size_t i = 0; i < lines.size(); ++i) {
                UI::Label line_lbl(lines[i], UI::ColorWhite);
                line_lbl.Draw(draw_x, desc_y + static_cast<int>(i) * line_height, BtlHelpWinAlpha);
            }
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

    if (s_quit_confirm_active && BtlHelpWinAlpha > 0) {
        DrawQuitConfirmModal(BtlHelpWinAlpha);
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

    if (s_quit_confirm_active) {
        if (GamePad.Down(PAD_UP | PAD_DOWN)) {
            s_quit_confirm_choice = 1 - s_quit_confirm_choice;
            ComMenuSePlay(MENU_SOUND_CURSOR);
        }

        if (GamePad.Down(PAD_CROSS)) {
            if (s_quit_confirm_choice == 0) {
                ComMenuSePlay(MENU_SOUND_CONFIRM);
                SDL_Event event{};
                event.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&event);
                return 1;
            } else {
                ComMenuSePlay(MENU_SOUND_REFUSE);
                s_quit_confirm_active = false;
                return 1;
            }
        }

        if (GamePad.Down(PAD_CIRCLE)) {
            ComMenuSePlay(MENU_SOUND_REFUSE);
            s_quit_confirm_active = false;
            return 1;
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

    if (GamePad.Down(PAD_CROSS)) {
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
                BtlWakuMake2 = 0;
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
                BtlWakuMake2 = 0;
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
                Btl_BattleManualInit(blocks, buffer);
                break;
            }
            case BTLMENU_PAGE_QUIT: {
                s_quit_confirm_active = true;
                s_quit_confirm_choice = 1;
                s_quit_cursor.SnapTo((640 - 120) / 2.0f - 34.0f, (164 + 58 + 10 + 28 + 6) + 4.0f);
                ComMenuSePlay(MENU_SOUND_CONFIRM);
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