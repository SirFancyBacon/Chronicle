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
#include "weapon_buildup.hpp"
#include "weaponlevelup.hpp"

// UI Framework
#include "ui_framework.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

extern void Btl_InitMenuChara(u_long128 *buffer);
extern void Btl_InitWeaponSelect(int mode, int chara);
extern void Btl_InitItemMode(int mode, int chara);
extern void Btl_BattleManualInit(int*, u_long128*);

#define BTLMENU_PAGE_LOAD 99
#define BTLMENU_PAGE_QUIT 100

namespace {

bool       s_quit_confirm_active = false;
int        s_quit_confirm_choice = 1;
UI::Cursor s_quit_cursor;
UI::Cursor s_move_cursor;
UI::Cursor s_wep_cursor;
int        s_last_move_state = -1;
char       g_frame_image_name[] = "frame_image";

void RefreshBtlMenuTextures() {
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
}

WEAPON_HAVE* GetNowSelectWeaponPort() {
    if (BtlMenuStatusPt == nullptr) return nullptr;
    return &BtlMenuStatusPt->chara_weapons[static_cast<int>(WepMenu.chara)][static_cast<int>(WepMenu.weapon_slot)];
}

int NowWeaponStatusValuePort(WEAPON_HAVE *weapon) {
    if (weapon == nullptr) return 0;

    int status = 0;
    if (weapon->experience >= GetWeaponMaxExp(weapon) || GetNowItemNum(ITEM_POWERUP_POWDER, MenuItemPackPt) > 0) {
        status |= 2;
    }
    if (WeaponStatusBreakEnable(weapon) != 0) {
        status |= 4;
    }
    status |= 8;

    int build_up = 0;
    if (WeaponStatusBuildUp(weapon, build_up) > 0 && GetNowWeaponAttachNum(weapon) == 0 && build_up > 0) {
        status |= 0x10;
    }
    if (IsNotBuildUpWeapon(weapon->item_no) != 0) {
        status &= ~8;
        status &= ~0x10;
    }
    return status;
}

std::string CleanDecodedText(std::string decoded) {
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
    return decoded;
}

std::string DecodeMesFromBuffer(short* mes_buffer, int mes_no) {
    if (mes_buffer == nullptr || mes_no < 0) return {};
    short* saved_buff   = CommonMenuMes3.buff;
    CommonMenuMes3.buff = mes_buffer;
    const short* codes  = CommonMenuMes3.GetTextLineDataTop(mes_no);
    CommonMenuMes3.buff = saved_buff;
    if (codes == nullptr) return {};
    return CleanDecodedText(GameTextDecode(codes));
}

std::string DecodeItemNamePort(int item_no, std::string_view fallback = {}) {
    if (item_no > 0 && SystemMes != nullptr) {
        if (COM_ITEM_INFO* info = GetCommonItemInfo(item_no)) {
            if (info->msg > 0) {
                std::string name = DecodeMesFromBuffer(SystemMes, info->msg + 0x64);
                size_t nl = name.find('\n');
                if (nl != std::string::npos) {
                    name.resize(nl);
                }
                if (!name.empty()) {
                    return name;
                }
            }
        }
    }
    return std::string(fallback);
}

std::string FormatWeaponTitlePort(const WEAPON_HAVE* weapon) {
    if (weapon == nullptr || weapon->item_no < ITEM_WEAPON_START) {
        return {};
    }
    std::string name = DecodeItemNamePort(weapon->item_no);
    if (name.empty()) {
        return {};
    }
    if (weapon->level > 0) {
        name += "+" + std::to_string(weapon->level);
    }
    return name;
}

void DrawWeaponTagBoardPort(int x, int y, WEAPON_HAVE *weapon, int build, int weapon_no, int alpha) {
    int page    = WepMenu.tag_page;
    int tag     = 0;
    int step    = 1;
    int wrapped = 0;

    for (int i = 0; i < 3; i++) {
        int brightness = 128;
        if (page == tag) {
            if (wrapped != 0) {
                tag = page;
            } else {
                tag     = 2;
                step    = -1;
                wrapped = 1;
            }
        }
        if (WepMenu.mode == WEP_MENU_ELEMENT && tag != WEP_TAG_ELEMENT) {
            brightness = 64;
        }
        int top = tag * 0xAA;
        DrawMenu2DSprite(WepStatus, CRect_i_(x, y, 0xD4, 0xAA), CRect_i_(0, top, 0xD4, 0xAA),
                         brightness, brightness, brightness, alpha);
        tag += step;
    }

    if (weapon != nullptr) {
        switch (page) {
            case WEP_TAG_STATUS:
                DrawWeaponStatusTag(x, y, weapon, build, weapon_no, alpha);
                break;
            case WEP_TAG_ELEMENT:
                DrawWeaponElemTag(x, y, weapon, build, weapon_no, alpha);
                break;
            case WEP_TAG_VS_MONSTER:
                DrawWeaponVsMonster(x, y, weapon, build, weapon_no, alpha);
                break;
        }
    }
}

void DrawWeaponLeftConfirmDialog(int box_x, int box_y, int yes_x, int yes_y,
                                 std::string_view fallback_prompt, int alpha) {
    std::string prompt = DecodeMesFromBuffer(CommonMenuMes1.buff, CommonMenuMes1.mes_made);
    if (prompt.empty()) {
        prompt = std::string(fallback_prompt);
    }

    std::vector<std::string> lines = UI::WrapText(prompt, 220, 0.92f);
    if (lines.empty()) {
        lines.emplace_back(prompt);
    }

    int max_w = 0;
    for (const auto& line : lines) {
        max_w = std::max(max_w, UI::Label(line, UI::ColorWhite, UI::Alignment::Left, true, 0.92f).GetWidth());
    }

    constexpr int kLineH = 20;
    const int     box_w  = std::clamp(max_w + 44, 212, 260);
    const int     box_h  = 34 + static_cast<int>(lines.size() - 1) * kLineH;

    UI::HelpBox::Draw(box_x, box_y, box_w, box_h, alpha);

    for (size_t i = 0; i < lines.size(); ++i) {
        UI::Label(lines[i], UI::ColorWhite, UI::Alignment::Center, true, 0.92f)
            .Draw(box_x + box_w / 2, box_y + 10 + static_cast<int>(i) * kLineH, alpha);
    }

    UI::ButtonStyle btn_style = UI::ButtonStyle::Thick(UI::ButtonPreset::Terracotta);
    btn_style.text_scale = 1.0f;

    const std::string yes_txt = LocalizeText("battlemenu.quit.yes", "YES");
    const std::string no_txt  = LocalizeText("battlemenu.quit.no",  "NO");

    UI::BevelButton::Draw(UI::Rect(yes_x, yes_y,        0x60, 24), yes_txt, btn_style, alpha);
    UI::BevelButton::Draw(UI::Rect(yes_x, yes_y + 0x1C, 0x60, 24), no_txt,  btn_style, alpha);
}

void DrawWeaponCenteredWarningModal(int alpha) {
    std::string msg = DecodeMesFromBuffer(CommonMenuMes1.buff, CommonMenuMes1.mes_made);
    if (msg.empty()) {
        CommonMenuMes1.auto_pos = MES_POS_CENTRE;
        MenuTextureReload(CommonMenuMes1.tex_block);
        CommonMenuMes1.Step();
        CommonMenuMes1.DrawMesWin();
        return;
    }

    std::vector<std::string> lines = UI::WrapText(msg, 320, 0.92f);
    int max_w = 0;
    for (const auto& line : lines) {
        max_w = std::max(max_w, UI::Label(line, UI::ColorWhite, UI::Alignment::Left, true, 0.92f).GetWidth());
    }

    constexpr int kLineH = 22;
    const int     box_w  = std::clamp(max_w + 52, 260, 380);
    const int     box_h  = 38 + static_cast<int>(lines.size() - 1) * kLineH;
    const int     box_x  = (640 - box_w) / 2;
    const int     box_y  = (480 - box_h) / 2 - 16;

    UI::HelpBox::Draw(box_x, box_y, box_w, box_h, alpha);

    for (size_t i = 0; i < lines.size(); ++i) {
        UI::Label(lines[i], UI::ColorWhite, UI::Alignment::Center, true, 0.92f)
            .Draw(320, box_y + 12 + static_cast<int>(i) * kLineH, alpha);
    }
}

int GetMenuModeMax() {
    // Keep retail's exact indices for 0..6 (Dungeon) and 0..7 (Town), and append Load & Quit at the end
    // so retail's BattleMenuStep / NorMenuIcon[0..7] state machine never desynchronizes!
    return (BtlMenuMode == BATTLE_MENU_MODE_DUNGEON) ? 9 : 10;
}

void BtlMenuMekeIconInfo(int *icons, int /*menu_mode*/) {
    // Indices 0..6 (Dungeon) and 0..7 (Town) match retail PS2 1:1.
    // Load (icon 5) and Quit (icon 12) sit at the bottom of the ring.
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

    // 1. When inside a submenu (or opening/closing a submenu), retail PS2's BattleMenuStep
    //    animates NorMenuIcon[index].x and .y directly:
    //    - For Item (0), Georama (3), Save (5), and Manual (6/7), retail slides NorMenuIcon[index]
    //      to the top-left header position (16, 36).
    //    - For Weapon (1), Allies (2), Move/World Map (4), and Options, retail slides NorMenuIcon[index]
    //      off-screen to the left (x < -100) so it does NOT cover the submenu UI!
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

    // 2. Main Battle Menu ring (with horizontal appear/exit slide from NorMenuIcon[0])
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

    // In LOAD mode, retail's DrawMenuSave already renders the oval icon + "LOAD" at (16, 36).
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

        // Skip drawing if retail slid this icon off-screen (e.g. Weapon, Allies, Move, Options)
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

void ExtractMovePromptText(std::string& out_title, std::string& out_desc) {
    out_title.clear();
    out_desc.clear();

    if (CommonMenuMes3.buff != nullptr && CommonMenuMes3.mes_made > 0) {
        std::string decoded = DecodeMesFromBuffer(CommonMenuMes3.buff, CommonMenuMes3.mes_made);
        const size_t nl     = decoded.find('\n');
        if (nl != std::string::npos) {
            out_title = decoded.substr(0, nl);
            out_desc  = decoded.substr(nl + 1);
        } else {
            out_title = decoded;
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

// =============================================================================
// WEAPON MENU OVERRIDES (Banners, Status Numbers, Action Buttons, & Dialogs)
// =============================================================================

PORT_OVERRIDE void DrawWepDamageDraw(RECT rect, WEAPON_HAVE *weapon, int alpha) {
    if (weapon == nullptr || alpha <= 0) return;

    const int x          = rect.x;
    int       y          = rect.y;
    const int durability = std::max(1, static_cast<int>(weapon->durability));
    const int now        = std::clamp(GetDispVolumeForFloat(weapon->durability_f), 0, durability);
    int       max_exp    = GetWeaponMaxExp(weapon);
    int       exp        = weapon->experience;

    if (max_exp < exp) exp = max_exp;
    if (exp < 0)       exp = 0;
    if (max_exp <= 0)  max_exp = 99;

    const int exp_len = (exp * 99) / max_exp;

#ifdef PAL
    MenuTextureReload(BtlMenuExtBlock);
#else
    MenuTextureReload(BtlMenuReadBlock);
#endif
    setbilinear(0);

    // 1. Original Vanilla WHp Bar
    DrawMenu2DSprite(WepStatus, CRect_i_(rect.x, y, durability, 8), CRect_i_(0x100, 0x70, 8, 8), alpha);
    MGFillBox(CRect_i_(rect.x * 16, ((y - 1) >> 1) * 16, (durability + 1) * 16, 0x50), 0x4F, 0x4F, 0x4F, alpha);

    GRADATION_COLOR_INFO2 colours;
    std::memcpy(&colours, GetGradationColorInfo2(25), sizeof(colours));
    colours.colors[0].a = static_cast<u8>(alpha);
    colours.colors[1].a = static_cast<u8>(alpha);
    colours.colors[2].a = static_cast<u8>(alpha);
    colours.colors[3].a = static_cast<u8>(alpha);
    CRect_i_ whp_fill(rect.x, y, now, 8);
    DrawMenuColorGradation(whp_fill,
                           &colours.colors[0], &colours.colors[1], &colours.colors[2], &colours.colors[3]);

    const int whp_text_y = y + rect.height - 5;

    // 2. Original Vanilla Abs Bar
    rect.y += 0x14;
    y       = rect.y;
    DrawMenu2DSprite(WepStatus, CRect_i_(rect.x, y, exp_len, 8), CRect_i_(0x100, 0x78, 8, 8), alpha);
    const int abs_text_y = y + rect.height - 5;

    // 3. Crisp Status Font Numbers ("12/28" and "67/98") at 0.64f scale (~18% smaller)
    const int slash_x = x + rect.width - 0x20;
    UI::DrawStatusSlashPair(now, durability, slash_x, whp_text_y, 0.64f, alpha);
    UI::DrawStatusSlashPair(exp, max_exp,    slash_x, abs_text_y, 0.64f, alpha);

    setbilinear(0);
#ifdef PAL
    MenuTextureReload(BtlMenuExtBlock);
#else
    MenuTextureReload(BtlMenuReadBlock);
#endif
}

PORT_OVERRIDE void DrawWepVolumeDisplay(int x, int y, WEAPON_HAVE *weapon, int alpha) {
    if (weapon == nullptr || alpha <= 0) return;
    WEAPON_DATA *data = GetWeaponData(weapon->item_no);
    if (data == nullptr) return;

    WEAPON_HAVE total{};
    WeaponAllValueSet(weapon, &total, 0);

    // 1. Elemental Sphere Icon + Crisp Element Abbreviation ("Fl", "Ch", "Th", "Wi", "Ho")
    if (weapon->best_elem >= 0 && weapon->best_elem < 5) {
        setbilinear(0);
        MenuTextureReload(BtlMenuReadBlock);
        DrawMenu2DSprite(MenuCharaFace,
                         CRect_i_(x, y - 0x10, 15, 16),
                         CRect_i_(0xE0, weapon->best_elem * 16, 15, 16),
                         alpha);

        static constexpr const char* kElemNames[5] = {"Fl", "Ch", "Th", "Wi", "Ho"};
        UI::DrawStatusText(kElemNames[static_cast<int>(weapon->best_elem)], x + 16, y - 0x12, UI::Alignment::Left,
                           240, 200, 55, 0.64f, alpha, 18, 34, 88);
    }

    // 2. Stat Labels ("At", "Ed", "Sp", "Mp") & Outlined Status Numbers
    int values[4] = {total.attack, total.endurance, total.speed, total.magic};
    const float rate = GetNowWeaponRate(&total);
    int limit[4] = {
        static_cast<int>(static_cast<float>(data->attack_max) * rate),
        99,
        99,
        static_cast<int>(static_cast<float>(data->magic_max) * rate)
    };

    struct StatLabelSpec {
        const char* name;
        u8 r, g, b;
        u8 out_r, out_g, out_b;
    };
    static constexpr StatLabelSpec kStatSpecs[4] = {
        {"At",  65, 235, 145,   8, 38, 36}, // Mint-Green (Attack)
        {"Ed", 235, 190,  35,  38, 32, 10}, // Warm Gold (Endurance)
        {"Sp",  75, 210, 215,   8, 36, 46}, // Seafoam Cyan-Blue (Speed)
        {"Mp", 225, 145, 215,  36, 18, 44}  // Rose-Lavender (Magic)
    };

    for (int i = 0; i < 4; i++) {
        if (values[i] > limit[i]) {
            values[i] = limit[i];
        }
        const int row_y = y + i * 16 - 2;
        UI::DrawStatusText(kStatSpecs[i].name, x, row_y, UI::Alignment::Left,
                           kStatSpecs[i].r, kStatSpecs[i].g, kStatSpecs[i].b,
                           0.66f, alpha,
                           kStatSpecs[i].out_r, kStatSpecs[i].out_g, kStatSpecs[i].out_b);
        UI::DrawStatusNumber(values[i], x + 48, row_y, 0.64f, alpha, false);
    }

    setbilinear(0);
#ifdef PAL
    MenuTextureReload(BtlMenuExtBlock);
#else
    MenuTextureReload(BtlMenuReadBlock);
#endif
}

PORT_OVERRIDE void DrawWeaponSelectDialog(int x, int y, int alpha) {
    const s8     shift  = (BtlMenuNowLang > LANG_JAPANESE) ? 2 : 0;
    const int    left   = x + shift;
    const int    top    = y + shift;
    WEAPON_HAVE *weapon = GetNowSelectWeaponPort();
    if (weapon == nullptr) return;

    const int repair_items = GetNowItemNum(ITEM_REPAIR_POWDER, MenuItemPackPt);
    const int options      = NowWeaponStatusValuePort(weapon);

    constexpr int kBtnW      = 0x60;
    constexpr int kBtnH      = 23;
    constexpr int kRowStride = 0x1A;

    UI::BevelButton::Draw(
        UI::Rect(left, top + 1, kBtnW, kBtnH),
        LocalizeText("weaponmenu.action.equip", "EQUIP"),
        UI::ButtonStyle::Thin(UI::ButtonPreset::Terracotta, 0x80),
        alpha
    );

    UI::BevelButton::Draw(
        UI::Rect(left, top + 1 + kRowStride, kBtnW, kBtnH),
        LocalizeText("weaponmenu.action.customize", "CUSTOMIZE"),
        UI::ButtonStyle::Thin(UI::ButtonPreset::Terracotta, 0x80),
        alpha
    );

    const int attr_x = (WepMenu.mode == WEP_MENU_ELEMENT) ? (left + 0x28) : left;
    UI::BevelButton::Draw(
        UI::Rect(attr_x, top + 1 + kRowStride * 2, kBtnW, kBtnH),
        LocalizeText("weaponmenu.action.attribute", "ATTRIBUTE"),
        UI::ButtonStyle::Thin(UI::ButtonPreset::Terracotta, 0x80),
        alpha
    );

    int repair_bright = 0x80;
    if (repair_items <= 0 || weapon->durability_f == static_cast<float>(weapon->durability)) {
        repair_bright = 0x48;
    }
    static int s_warm_cnt = 0;
    const int  default_no = GetDefaultWeaponNo(WepMenu.chara);
    if ((weapon->durability_f <= 0.1f * weapon->durability || weapon->item_no == default_no) && repair_items > 0) {
        repair_bright = std::abs(static_cast<int>(50.0f * std::sin(static_cast<float>(s_warm_cnt) / 12.0f))) + 0x78;
        s_warm_cnt    = (s_warm_cnt + 1) % 1000000;
    }

    UI::BevelButton::Draw(
        UI::Rect(left, top + 1 + kRowStride * 3, kBtnW, kBtnH),
        LocalizeText("weaponmenu.action.repair", "REPAIR"),
        UI::ButtonStyle::Thin(UI::ButtonPreset::Terracotta, repair_bright),
        alpha
    );

    static int s_level_blink_cnt = 0;
    s_level_blink_cnt = (s_level_blink_cnt + 1) % 1000000;

    int dest_y = top + 1 + kRowStride * 4;
    for (int i = 1; i <= 3; i++) {
        int row_bright = 0x80;
        if (i == 3) {
            int build = 0;
            WeaponStatusBuildUp(weapon, build);
            const int attached = GetNowWeaponAttachNum(weapon);
            if (build > 0 && attached == 0) {
                row_bright += std::abs(static_cast<int>(40.0f * std::sin(static_cast<float>(s_level_blink_cnt) / 12.0f)));
            }
        }

        if (options & (1 << i)) {
            std::string      btn_label;
            UI::ButtonPreset btn_preset = UI::ButtonPreset::AmberGold;
            if (i == 1) {
                btn_label  = LocalizeText("weaponmenu.action.upgrade", "UPGRADE");
                btn_preset = UI::ButtonPreset::AmberGold;
            } else if (i == 2) {
                btn_label  = LocalizeText("weaponmenu.action.synthesize", "SYNTHESIZE");
                btn_preset = UI::ButtonPreset::AmberGold;
            } else {
                btn_label  = LocalizeText("weaponmenu.action.buildup", "BUILD-UP");
                btn_preset = UI::ButtonPreset::StoneGreen;
            }

            UI::BevelButton::Draw(
                UI::Rect(left, dest_y, kBtnW, kBtnH),
                btn_label,
                UI::ButtonStyle::Thin(btn_preset, row_bright),
                alpha
            );
            dest_y += kRowStride;
        }
    }

    if (WepMenu.state == WEP_STATE_SCRAP_PROMPT || WepMenu.state == WEP_STATE_SCRAP_CONFIRM) {
        UI::BevelButton::Draw(
            UI::Rect(x + 0x64, y + 0x33, kBtnW, kBtnH),
            LocalizeText("weaponmenu.action.discard", "DISCARD"),
            UI::ButtonStyle::Thin(UI::ButtonPreset::Terracotta, 0x80),
            0x80
        );
    }
}

PORT_OVERRIDE void RepairAndLevelUpDraw(int x, int y, int alpha) {
    if (alpha <= 0) return;

    setbilinear(0);
    MenuTextureReload(BtlMenuReadBlock);

    constexpr int kBoxW = 248;
    constexpr int kBoxH = 82;
    UI::HelpBox::Draw(x - 10, y, kBoxW, kBoxH, alpha);

    const int   items[2] = {ITEM_REPAIR_POWDER, ITEM_POWERUP_POWDER};
    const char* kKeys[2] = {"weaponmenu.repair_powder", "weaponmenu.powerup_powder"};
    const char* kDefs[2] = {"Repair Powder",            "Powerup Powder"};

    for (int i = 0; i < 2; i++) {
        const int item_no = items[i];
        const int count   = GetNowItemNum(item_no, MenuItemPackPt);
        const int row_y   = y + 11 + i * 32;

        UI::ItemIcon::Draw(item_no, x + 6, row_y, 26, alpha, true, false);

        std::string item_name = DecodeItemNamePort(item_no, LocalizeText(kKeys[i], kDefs[i]));
        for (char& ch : item_name) {
            if (ch == '\n' || ch == '\r') ch = ' ';
        }

        UI::Label name_lbl(item_name, UI::ColorWhite, UI::Alignment::Left, true, 0.80f);
        if (name_lbl.GetWidth() > 130) {
            name_lbl.SetXScale(130.0f / static_cast<float>(name_lbl.GetWidth()));
        }
        name_lbl.Draw(x + 38, row_y + 2, alpha);

        const u32 count_col = (count > 0) ? UI::ColorGold : UI::ColorWhite;
        UI::Label("x", UI::ColorWhite, UI::Alignment::Center, true, 0.80f).Draw(x + 182, row_y + 2, alpha);
        UI::Label(std::to_string(count), count_col, UI::Alignment::Right, true, 0.86f).Draw(x + 216, row_y + 2, alpha);
    }

    setbilinear(0);
    MenuTextureReload(BtlMenuReadBlock);
}

PORT_OVERRIDE void DrawBuildUpWeaponSelect(int x, int y, int cursor) {
    WEAPON_HAVE* weapon = GetNowSelectWeaponPort();
    if (weapon == nullptr) return;

    int build = 0;
    const int count = WeaponStatusBuildUp(weapon, build);

    WEP_BUILDUP_INFO builds[8]{};
    EnableBuildUpModel(builds, weapon);

    std::string header = DecodeMesFromBuffer(CommonMenuMes1.buff, 0x75);
    if (header.empty()) {
        header = LocalizeText("weaponmenu.buildup.prompt", "Select weapon to build up to:");
    }

    std::vector<std::string> h_lines = UI::WrapText(header, 220, 0.86f);
    const int header_h = std::max(1, static_cast<int>(h_lines.size())) * 20 + 14;
    const int box_w    = 252;
    const int box_h    = header_h + count * 0x26 + 22;

    UI::HelpBox::Draw(x - 14, y - 12, box_w, box_h, 0x80);

    for (size_t i = 0; i < h_lines.size(); ++i) {
        UI::Label(h_lines[i], UI::ColorWhite, UI::Alignment::Left, true, 0.86f)
            .Draw(x + 4, y + static_cast<int>(i) * 20, 0x80);
    }

    int row_y = y + CommonMenuMes1.char_height + 0x1E;
    for (int i = 0; i < count; i++) {
        const bool selected   = (i == cursor);
        const int  brightness = selected ? 0x88 : 0x68;

        std::string target_name = "???";
        u32         text_col    = UI::ColorWhite;
        if (builds[i].enabled != 0 && builds[i].weapon_no > 0) {
            target_name = DecodeItemNamePort(builds[i].weapon_no, "???");
            text_col    = selected ? UI::ColorYellow : UI::ColorWhite;
        }

        UI::TextStyle banner_txt{
            .color       = text_col,
            .drop_shadow = true,
            .align       = UI::Alignment::Center,
            .scale       = 0.90f
        };

        UI::TrackerWindow::DrawStoneBanner(
            UI::Rect(x + 0x10, row_y, 0xCC, 0x1F),
            0x80,
            brightness,
            target_name,
            banner_txt
        );

        row_y += 0x26;
    }
}

PORT_OVERRIDE void DrawAallWeapon(int x, int y, float depth, CCharacter *model,
                                  WEAPON_HAVE *weapon, int equipped, int selected, int alpha) {
    if (weapon == nullptr) return;

    MenuTextureReload(MenuExtendReadBlock);
    float         spin     = 0.0f;
    sceVu0FVECTOR position = {depth, -1.3f, 0.0f, 1.0f};

    if (model != nullptr) {
        if (equipped != 0) {
            position[1] = -1.3f + std::sin(WepFrameRate);
            WepFrameRate = std::min(static_cast<float>(PI), WepFrameRate + 0.34906587f);
            if (static_cast<float>(PI) != WepFrameRate) {
                spin = 0.0f;
            }
        }

        MENU_WEP_POLY_OFFSET *offset = &MenuWepPolyOffset[static_cast<int>(WepMenu.chara)];
        position[0] += offset->position[0];
        position[1] += offset->position[1];
        model->SetPosition(position);
        const float scale = offset->scale;
        model->SetScale(scale, scale, scale);

        if (selected != 0) {
            spin = static_cast<float>(PI_D * (CursorVibeCnt % 219 - 110) / 110);
        }

        if (MenuWepLevelUp.operation_kind == WEP_EFFECT_NONE ||
            MenuWepLevelUp.operation_kind == WEP_EFFECT_RECOVER ||
            MenuWepLevelUp.operation_kind == WEP_EFFECT_BUILDUP) {
            float rot_x = -HALF_PI;
            if (rot_x > PI_D) rot_x -= PI;
            if (spin > PI_D)  spin  -= PI;
            float rot_z = 0.62831855f;
            if (rot_z > PI_D) rot_z -= PI;
            model->SetRotation(rot_x, spin, rot_z);
        }

        model->Draw();
    }

#ifdef PAL
    MenuTextureReload(BtlMenuExtBlock);
#else
    MenuTextureReload(BtlMenuReadBlock);
#endif

    switch (WepMenu.mode) {
        case WEP_MENU_LIST:
        case WEP_MENU_ACTION:
        case WEP_MENU_ELEMENT:
        case WEP_MENU_LEVELUP_CONFIRM:
        case WEP_MENU_STATUS_BREAK_CONFIRM:
        case WEP_MENU_BUILDUP_SELECT:
        case WEP_MENU_BUILDUP_CONFIRM:
        case WEP_MENU_BUILDUP_PREVIEW:
            DrawWepStatus(x + 2, y + 0x66, weapon, equipped, alpha);
            DrawWepVolumeDisplay(x, y + 0x14, weapon, alpha);
            break;
        default:
            break;
    }

    const bool hide_title =
        ((WepMenu.state == WEP_STATE_STATUS_BREAK_EFFECT || WepMenu.state == WEP_STATE_BUILDUP_EFFECT) && selected != 0) ||
        (WepMenu.mode > WEP_MENU_LIST && selected == 0) ||
        (WepMenu.state != WEP_STATE_REPAIR_EFFECT && WepMenu.state >= WEP_STATE_STATUS_BREAK_EFFECT && WepMenu.state < WEP_STATE_WARNING) ||
        (WepMenu.state == WEP_STATE_LEVELUP_EFFECT && selected == 0);

    const std::string weapon_title = hide_title ? std::string{} : FormatWeaponTitlePort(weapon);

    UI::TextStyle title_style{
        .color       = UI::ColorGold,
        .drop_shadow = true,
        .align       = UI::Alignment::Center,
        .scale       = 1.14f,
        .text_dy     = 1
    };

    UI::TrackerWindow::DrawStoneBanner(
        UI::Rect(x - 0x11, y - 0x31, 0xCC, 0x1F),
        alpha,
        0x80,
        weapon_title,
        title_style
    );

    setbilinear(0);
#ifdef PAL
    MenuTextureReload(BtlMenuExtBlock);
#else
    MenuTextureReload(BtlMenuReadBlock);
#endif

    if (equipped != 0) {
        DrawMenu2DSprite(WepStatus, CRect_i_(x + 10, y - 0x15, 0x40, 0x17), CRect_i_(0xD4, 0x141, 0x40, 0x17), alpha);
    }

    WeaponStarDraw(x + 4, y + 0x3C, weapon, alpha);

    if (selected != 0) {
        MenuMes.draw_x = x + 0xA0;
        MenuMes.draw_y = y + 0xE;
    } else {
        MenuMes.draw_x = 0x280;
        MenuMes.draw_y = 0;
        setbilinear(0);
        MenuTextureReload(BtlMenuReadBlock);
        WeaponOptionStatusDraw(weapon, x + 0x50, y - 0x12, alpha);
    }

    const float hole_scale[2] = {0.75f, 1.0f};
    float       width         = static_cast<float>(GetWeaponHoleNum(weapon->item_no) * 0x12 + 10) * hole_scale[selected];
    const int   draw_x        = static_cast<int>(static_cast<float>(x + 0x52) - width);

    if (WepMenu.mode >= WEP_MENU_ATTACH_WEAPON) {
        DrawWepHole(draw_x, 0xB8, weapon, selected, alpha);
        MenuTextureReload(MenuShadowReadBlock);
        DrawWepAttach(draw_x + 0xE, 0xBE, weapon, selected, alpha);
    }
}

PORT_OVERRIDE void BtlWeaponDraw(int x, float depth, int chara, int alpha) {
    const int     equipped_slot = BtlMenuStatusPt->equipped_weapon_slot[chara];
    const int     cursor        = WepMenu.weapon_slot;
    sceVu0FVECTOR ref           = {0.0f, 0.0f, -45.0f, 1.0f};
    sceVu0FVECTOR pos           = {0.0f, 0.0f, 60.0f, 1.0f};
    sceVu0FVECTOR ambient;
    sceVu0FVECTOR bright;
    sceVu0FMATRIX light_dir;
    sceVu0FMATRIX light_col;

    MenuCamera.SetRef(ref);
    MenuCamera.SetPos(pos);
    MGGetPLight(light_dir, light_col);
    MGGetAmbient(ambient);
    bright[0] = bright[1] = bright[2] = 80.0f;
    sceVu0FVECTOR direction = {0.3f, 1.0f, 0.3f, 0.0f};
    sceVu0FVECTOR normal;
    sceVu0Normalize(normal, direction);
    sceVu0FMATRIX dir = {
        {normal[0], 0.0f, 0.0f, 0.0f},
        {normal[1], 0.0f, 0.0f, 0.0f},
        {normal[2], 0.0f, 0.0f, 0.0f},
        {0.0f,      0.0f, 0.0f, 0.0f}
    };
    sceVu0FMATRIX col = {
        {96.0f, 96.0f, 96.0f, 0.0f},
        {0.0f,  0.0f,  0.0f,  0.0f},
        {0.0f,  0.0f,  0.0f,  0.0f},
        {0.0f,  0.0f,  0.0f,  0.0f}
    };
    MGSetPLight(dir, col);

    for (int i = 0; i < 10; i++, x += 0xD6, depth += 16.0f) {
        int draw_alpha = alpha;

        if (x < -0x60 || (WepMenu.mode >= WEP_MENU_ATTACH_WEAPON && i != cursor)) {
            continue;
        }
        if (x >= 0x277) {
            break;
        }

        WEAPON_HAVE *weapon      = &DngWepHavePt[i];
        const int    is_equipped = (i == equipped_slot) ? 1 : 0;
        const int    is_selected = (i == cursor) ? 1 : 0;

        if (is_selected != 0) {
            if (WepMenu.state == WEP_STATE_STATUS_BREAK_EFFECT) {
                if (MenuWepLevelUp.effect_state == WEP_EFFECT_STATE_BREAK_PLAY) {
                    draw_alpha = std::max(0, static_cast<int>(128.0f - 4.0f * MenuWepLevelUp.effect_timer));
                } else if (MenuWepLevelUp.effect_state == WEP_EFFECT_STATE_BREAK_DONE) {
                    draw_alpha = MenuWepLevelUp.lost_default_weapon ? 0 : std::min(0x80, static_cast<int>(MenuWepLevelUp.effect_timer));
                }
            }
            MenuMes.alpha = draw_alpha;
        } else {
            if (WepMenu.mode > WEP_MENU_LIST || WepMenu.state >= WEP_STATE_LEVELUP_EFFECT) {
                continue;
            }
        }

        bright[3] = static_cast<float>(draw_alpha);
        MGSetAmbient(bright);

        if (MenuExTextureReadFlag != MENU_EX_TEX_READY) {
            continue;
        }

        if (weapon->item_no >= ITEM_WEAPON_START) {
            CCharacter *frame = nullptr;
            if (GetNowTestNo() == 1) {
                if (is_equipped != 0) {
                    MenuTextureReload(MenuShadowReadBlock);
                    const char *shadows[6] = {"kagetoan", "kagesyao", "kagegoro", "kageruby", "kageunga", "kageozu"};
                    DrawMenu2DSprite(TexManager.GetTexture(const_cast<char*>(shadows[static_cast<int>(WepMenu.chara)]), -1),
                                     CRect_i_(x + 0x24, 0x96, 0x64, 0x63),
                                     CRect_i_(0, 0, 0x64, 0x64), draw_alpha * 9 / 40);
                    MenuTextureReload(MenuExtendReadBlock);
                }

                const int frame_no = GetMenuWeaponModelFrameNo(i);
                if (frame_no >= 0) {
                    frame = &DngWeaponFrm[frame_no];
                    MenuWeaponSpSet(frame, weapon);
                }
            }

            if (frame != nullptr) {
                DrawAallWeapon(x, 0xA0, depth, frame, weapon, is_equipped, is_selected, draw_alpha);
            }
        } else {
#ifdef PAL
            MenuTextureReload(BtlMenuExtBlock);
#else
            MenuTextureReload(BtlMenuReadBlock);
#endif
            if (is_selected == 0 || WepMenu.state != WEP_STATE_STATUS_BREAK_EFFECT) {
                DrawMenuNothing(x - 4, 0x82, 0xB4, 0x96, (char*)"wepstatus", 0, draw_alpha);
            }
        }

        MenuMes.mode = MENU_CLSMES_OFF;

        switch (WepMenu.mode) {
            case WEP_MENU_ATTACH_TAGS:
            case WEP_MENU_ATTACH_SOCKETS:
            case WEP_MENU_ATTACH_BOARD:
            case WEP_MENU_ATTACH_WEAPON:
                MenuTextureReload(BtlMenuReadBlock);
                DrawBtlMenuLRCursor(x - 0x1C, 0xE6, 0xD4, draw_alpha);
#ifdef PAL
                MenuTextureReload(BtlMenuExtBlock);
#endif
                DrawWeaponTagBoardPort(x - 0xC, 0xF0, weapon, 0, 0, draw_alpha);
                [[fallthrough]];
            case WEP_MENU_LIST:
                setbilinear(0);
                MenuTextureReload(BtlMenuReadBlock);
                WeaponOptionStatusDraw(weapon, x + 0x50, 0x8E, draw_alpha);
                break;
            default:
                MenuMes.mode = MENU_CLSMES_OPTIONS;
                break;
        }
    }

    MGSetPLight(light_dir, light_col);
    MGSetAmbient(ambient);

    switch (WepMenu.mode) {
        case WEP_MENU_ATTACH_TAGS:
        case WEP_MENU_ATTACH_SOCKETS:
        case WEP_MENU_ATTACH_BOARD:
        case WEP_MENU_ATTACH_WEAPON:
            break;
        default: {
            const int dim = (WepMenu.mode != WEP_MENU_LIST) ? 1 : 0;
            FadeTexX(0, 0xCC, 0x1AE, 0xD2, (char*)"frame_image", dim);
            break;
        }
    }

    if (WepMenu.state != WEP_STATE_STATUS_BREAK_EFFECT) {
        MenuMes.Step();
        MenuMes.Draw1(MenuMes.draw_x, MenuMes.draw_y, MenuMes.alpha);
    }
}

PORT_OVERRIDE void WeaponMenuDraw() {
    RefreshBtlMenuTextures();
    int alpha = 0x80;

    if (WepMenu.state == WEP_STATE_OPEN) {
        alpha = std::min(0x80, static_cast<int>(WepMenu.counter * 9));
    } else if (WepMenu.state == WEP_STATE_CLOSE) {
        alpha = std::max(0, static_cast<int>(0x80 - WepMenu.counter * 9));
    }

    int attach_mode = 0;
    switch (WepMenu.mode) {
        case WEP_MENU_ATTACH_WEAPON:
        case WEP_MENU_ATTACH_SOCKETS:
        case WEP_MENU_ATTACH_BOARD:
        case WEP_MENU_ATTACH_TAGS:
            attach_mode = 1;
            [[fallthrough]];
        case WEP_MENU_LIST:
        case WEP_MENU_ACTION:
        case WEP_MENU_ELEMENT:
        case WEP_MENU_LEVELUP_CONFIRM:
        case WEP_MENU_STATUS_BREAK_CONFIRM:
        case WEP_MENU_BUILDUP_SELECT:
        case WEP_MENU_BUILDUP_CONFIRM:
        case WEP_MENU_BUILDUP_PREVIEW:
            for (int i = 0; i < 6; i++) {
                const int target = (SysChara[i].chara - WepMenu.chara) * 0x6E + 0x10E;
                SysChara[i].x += (static_cast<float>(target) - SysChara[i].x) / 4.0f;

                if (attach_mode == 0 || i == WepMenu.chara) {
                    CTexture *face      = MenuCharaFace;
                    int       scratch   = 0;
                    const int face_char = SysChara[i].chara;
                    int       v         = face_char * 0x6A;

                    if (face_char > 2) {
                        scratch = 0x6A;
                        v       = (face_char - 3) * 0x6A;
                    }
                    if (BtlMenuStatusPt->party_size <= i) {
                        face    = BtStatus;
                        v       = 0x8C;
                        scratch = 0x8C;
                    }

                    DrawMenu2DSprite(face, CRect_i_(SysChara[i].x, 0xE, 0x64, 0x64),
                                     CRect_i_(scratch, v, 0x6A, 0x6A), alpha, alpha, alpha, alpha);
                }
            }

            FadeTexX(0xDC, 0x32, 0x172, 0x32, (char*)"frame_image", 0);

            if (attach_mode == 0) {
                static int s_arrow_ct = 0;
                int scratch = alpha;
                if (WepMenu.mode == WEP_MENU_LIST) {
                    s_arrow_ct = (s_arrow_ct + 1) % 1000000;
                } else {
                    scratch = 0;
                }
                if (s_arrow_ct % 50 < 30) {
                    const int step = (s_arrow_ct % 30) / 10 * 2;
                    CRect_i_  arrow(0x60, 0x68, 10, 0x18);
                    DrawMenu2DSprite(BtStatus, CRect_i_(0x102 - step, 0x48, arrow.width, arrow.height), arrow, scratch);
                    arrow.x += arrow.width;
                    DrawMenu2DSprite(BtStatus, CRect_i_(step + 0x174, 0x48, arrow.width, arrow.height), arrow, scratch);
                }
            }
            break;
    }

    int   target_x     = 0;
    float target_depth = 0.0f;
    if (WepMenu.mode >= WEP_MENU_ATTACH_WEAPON) {
        const int slot = WepMenu.weapon_slot;
        target_x       = static_cast<int>(80.0f - static_cast<float>(slot * 0xD6));
        target_depth   = 2.0f + (-11.0f + 16.0f * -slot);
    } else {
        target_x     = 0xE8 - WepMenu.weapon_slot * 0xD6;
        target_depth = 2.0f + 16.0f * -WepMenu.weapon_slot;
    }

    WeaponPos  += (static_cast<float>(target_x) - WeaponPos) / 4.0f;
    WepPolyPos += (target_depth - WepPolyPos) / 4.0f;

    int scratch = alpha;
    if (WepMenu.state == WEP_STATE_UNK_3) {
        scratch = alpha - 9;
    } else if (WepMenu.state == WEP_STATE_CHARA_CHANGE) {
        scratch = WepMenu.counter * 5;
    }
    scratch = std::clamp(scratch, 0, 0x80);

    BtlWeaponDraw(static_cast<int>(WeaponPos), WepPolyPos, WepMenu.chara, scratch);

    if (attach_mode == 0) {
#ifdef PAL
        const u8 repair_x[7] = {78, 78, 78, 26, 64, 64, 66};
        RepairAndLevelUpDraw(repair_x[BtlMenuNowLang], 0x164, alpha);
#else
        RepairAndLevelUpDraw(0x4E, 0x144, alpha);
#endif
    }

    constexpr int dialog_pos = 0x70;
    WEP_BUILDUP_INFO builds[4]{};

    switch (WepMenu.mode) {
        case WEP_MENU_LEVELUP_CONFIRM:
            DrawWeaponLeftConfirmDialog(0x46, 0x8C, 0x88, 0xDD,
                                        LocalizeText("weaponmenu.upgrade.confirm", "Upgrade. Confirm?"), 0x80);
            break;
        case WEP_MENU_BUILDUP_CONFIRM:
            DrawWeaponLeftConfirmDialog(0x46, 0x8C, 0x88, 0xDD,
                                        LocalizeText("weaponmenu.buildup.confirm", "Build-up. Confirm?"), 0x80);
            break;
        case WEP_MENU_STATUS_BREAK_CONFIRM:
            DrawWeaponLeftConfirmDialog(0x46, 0x88, 0x88, 0xF3,
                                        LocalizeText("weaponmenu.synthesize.confirm", "Status break this weapon?\nWeapon disappears."), 0x80);
            break;
        case WEP_MENU_BUILDUP_PREVIEW:
            AllFadeForMenu(0x40);
            MenuTextureReload(BtlMenuReadBlock);
            DrawBtlMenuLRCursor(0x158, 0x6E, 0xD4, alpha);
            EnableBuildUpModel(builds, GetNowSelectWeaponPort());
#ifdef PAL
            MenuTextureReload(BtlMenuExtBlock);
#endif
            DrawWeaponTagBoardPort(0x16C, 0x78, GetNowSelectWeaponPort(), 1, builds[WepMenu.board.cursor].weapon_no, 0x80);
            [[fallthrough]];
        case WEP_MENU_BUILDUP_SELECT:
            DrawBuildUpWeaponSelect(0x58, 0x9C, WepMenu.board.cursor);
            break;
        case WEP_MENU_ELEMENT:
#ifdef PAL
            MenuTextureReload(BtlMenuExtBlock);
#endif
            DrawWeaponTagBoardPort(0x18A, 0x64, GetNowSelectWeaponPort(), 0, 0, 0x80);
            [[fallthrough]];
        case WEP_MENU_ACTION:
            if (GetNowSelectWeaponPort() != nullptr) {
                DrawWeaponSelectDialog(dialog_pos, dialog_pos, scratch);
            }
            break;
        case WEP_MENU_LIST:
            if (BtlMenuStatusPt->party_size > 1) {
                MenuTextureReload(BtlMenuReadBlock);
                DrawBtlMenuLRCursor(0xE6, 0x12, 0x96, alpha);
            }
            break;
        case WEP_MENU_ATTACH_WEAPON:
        case WEP_MENU_ATTACH_SOCKETS:
        case WEP_MENU_ATTACH_BOARD:
        case WEP_MENU_ATTACH_TAGS:
            MenuTextureReload(BtlMenuReadBlock);
            DrawPersonalBoard(0x154, 0x78, WepMenu.board.page, alpha, 0);
            CommonTrushDraw(0x232, 0x10C, alpha);
            if (BtlHaveItemPt->item_no >= ITEM_ATTACH_START) {
                MenuTextureReload(MenuShadowReadBlock);
                DrawMenuVibeItem(static_cast<int>(SysCur[0]), static_cast<int>(SysCur[1]), 2, -0xC, 0x80);
            }
            break;
    }

    int slot = std::clamp(static_cast<int>(WepMenu.weapon_slot), 0, 9);
    WepMenu.weapon_slot = static_cast<s8>(slot);

    const int holes = GetWeaponHoleNum(DngWepHavePt[slot].item_no);
    s16 cursor_pos[12][2] = {
        {0xC0,  0xA5},
        {0x50,  0   },
        {0x16E, 0   },
        {0x68,  0   },
        {0x68,  0   },
        {0x4E,  0   },
        {0x68,  0   },
        {0x4E,  0   },
        {0x58,  0xAA},
        {0,     0xB8},
        {0,     0   },
        {0,     0   }
    };
    const s16 row      = static_cast<s16>(WepMenu.board.cursor * 0x1A);
    cursor_pos[1][1]   = dialog_pos + row;
    cursor_pos[2][1]   = static_cast<s16>(WepMenu.element) * 0x18 + 0x86;
    const s16 dialog_y = row + 0xDC;
    cursor_pos[3][1]   = dialog_y;
    cursor_pos[4][1]   = row + 0xF2;
    const s16 build_y  = CommonMenuMes1.char_height + static_cast<s16>(WepMenu.board.cursor * 0x26 + 0xBA);
    cursor_pos[5][1]   = build_y;
    cursor_pos[6][1]   = dialog_y;
    cursor_pos[7][1]   = build_y;
    cursor_pos[9][0]   = (0x90 - holes * 0x12) + static_cast<s16>(WepMenu.board.cursor * 0x2A);

    int cursor_x = cursor_pos[WepMenu.mode][0];
    int cursor_y = cursor_pos[WepMenu.mode][1];

    if (WepMenu.state == WEP_STATE_SCRAP_PROMPT) {
        cursor_x = 0xB4;
        cursor_y = dialog_pos + 0x2E;
    }
    if (WepMenu.state == WEP_STATE_SCRAP_CONFIRM) {
        cursor_x = 0x171;
        cursor_y = static_cast<int>(WepMenu.confirm[0]) * 0x1A + 0xBE;
    }

    RECT frames[12] = {
        {0xCD, 0x64, 0xCC, 0xC9},
        {0,    0,    0x60, 0x18},
        {0,    0,    0xC4, 0x18},
        {0,    0,    0x60, 0x18},
        {0,    0,    0x60, 0x18},
        {0,    0,    0xC0, 0x20},
        {0,    0,    0x60, 0x18},
        {0,    0,    0xC0, 0x20},
        {0,    0,    0x6E, 0x6E},
        {0,    0,    0x28, 0x28},
        {0,    0,    0x26, 0x26},
        {0xD8, 0x76, 0xB8, 0x98}
    };
    frames[1].x  = cursor_x + 0x19;
    frames[1].y  = cursor_y - 6;
    frames[2].x  = cursor_x + 0x1A;
    frames[2].y  = cursor_y - 6;
    frames[3].x  = cursor_x + 0x18;
    frames[3].y  = cursor_y - 6;
    frames[4].x  = cursor_x + 0x18;
    frames[4].y  = cursor_y - 6;
    frames[5].x  = cursor_x + 0x18;
    frames[5].y  = cursor_y - 8;
    frames[6].x  = cursor_x + 0x18;
    frames[6].y  = cursor_y - 6;
    frames[7].x  = cursor_x + 0x18;
    frames[7].y  = cursor_y - 8;
    frames[8].x  = cursor_x + 0x12;
    frames[8].y  = cursor_y - 0x18;
    frames[9].x  = cursor_x + 0xC;
    frames[9].y  = cursor_y - 8;
    frames[10].x = cursor_x + 0xC;
    frames[10].y = cursor_y - 8;

    RECT frame = frames[WepMenu.mode];
    WEAPON_HAVE *selected = GetNowSelectWeaponPort();
    if (selected != nullptr && selected->item_no < ITEM_WEAPON_START) {
        frame = frames[11];
    }

    if (WepMenu.state == WEP_STATE_SCRAP_PROMPT) {
        frame.x      = cursor_x + 0x19;
        frame.y      = cursor_y - 4;
        frame.width  = 0x60;
        frame.height = 0x18;
    } else if (WepMenu.state == WEP_STATE_SCRAP_CONFIRM) {
        frame.x      = 0x190 - 6;
        frame.y      = cursor_y - 4;
        frame.width  = 0x60;
        frame.height = 0x18;
    }

    switch (WepMenu.mode) {
        case WEP_MENU_ACTION:
            if (WepMenu.board.cursor >= WEP_ACTION_LEVELUP &&
                WepMenu.state != WEP_STATE_SCRAP_PROMPT &&
                WepMenu.state != WEP_STATE_SCRAP_CONFIRM) {
                int offset_y = 0;
                const int options = NowWeaponStatusValuePort(&DngWepHavePt[static_cast<int>(WepMenu.weapon_slot)]);
                for (int i = 4; WepMenu.board.cursor >= i; i++) {
                    if (options & (1 << (i - 3))) {
                        offset_y += 0x1A;
                    }
                }
                cursor_y = dialog_pos + 0x4E + offset_y;
                frame.y  = cursor_y - 6;
            }
            break;
        case WEP_MENU_ATTACH_BOARD:
            if (WepMenu.board.cursor_area == PERSONAL_BOARD_AREA_CELLS) {
                int cell = WepMenu.board.cursor;
                cursor_x = (cell % 5) * 0x28 + 0x154;
                cell    -= WepMenu.board.top_row * 5;
                cursor_y = (cell / 5) * 0x28 + 0x7E;
            } else if (WepMenu.board.cursor_area == PERSONAL_BOARD_AREA_TRASH) {
                cursor_x = 0x220;
                cursor_y = 0x110;
            }
            frame.x      = cursor_x + 0xC;
            frame.y      = cursor_y - 8;
            frame.height = 0x26;
            frame.width  = 0x26;
            break;
        case WEP_MENU_ATTACH_TAGS: {
            const int sel = WepMenu.tag_row;
            if (WepMenu.tag_page == WEP_TAG_STATUS) {
                if (sel < 2) {
                    cursor_x    = 0x24;
                    cursor_y    = sel * 0x14 + 0x114;
                    frame.width = 0xCA;
                } else {
                    cursor_x    = 0x2A;
                    cursor_y    = (sel - 2) * 0x10 + 0x148;
                    frame.width = 0xC4;
                }
            } else if (WepMenu.tag_page == WEP_TAG_ELEMENT) {
                cursor_x    = 0x2A;
                cursor_y    = sel * 0x18 + 0x112;
                frame.width = 0xC4;
            } else if (WepMenu.tag_page == WEP_TAG_VS_MONSTER) {
                cursor_x    = (sel / 5) * 0x68 + 0x28;
                cursor_y    = (sel % 5) * 0x18 + 0x112;
                frame.width = 0x66;
            }
            frame.x      = cursor_x + 0x1C;
            frame.y      = cursor_y - 6;
            frame.height = 0x18;
            break;
        }
        default:
            break;
    }

    DNG_CONSUMABLE *consumables = BtlMenuStatusPt->consumable_items;
    if (WepMenu.mode <= WEP_MENU_LIST) {
        scratch = 0x40;
    } else if (BtlHaveItemPt->item_no >= ITEM_ATTACH_START) {
        cursor_x += 0x12;
        cursor_y += 0xE;
        scratch   = 0x80;
    } else if ((WepMenu.mode == WEP_MENU_ATTACH_SOCKETS && DngWepHavePt[slot].attach[WepMenu.board.cursor].item_no >= ITEM_ATTACH_START) ||
               (WepMenu.mode == WEP_MENU_ATTACH_BOARD && consumables[WepMenu.board.cursor].id >= ITEM_ATTACH_START)) {
        cursor_x += 10;
        cursor_y += 0x18;
        scratch   = 0x60;
    } else {
        scratch = 0x40;
    }

    SysCur[0] += (static_cast<float>(cursor_x) - SysCur[0]) / 4.0f;
    SysCur[1] += (static_cast<float>(cursor_y) - SysCur[1]) / 4.0f;

    switch (WepMenu.state) {
        case WEP_STATE_OPEN:
        case WEP_STATE_CLOSE:
        case WEP_STATE_CHARA_CHANGE:
        case WEP_STATE_LEVELUP_EFFECT:
        case WEP_STATE_STATUS_BREAK_EFFECT:
        case WEP_STATE_BUILDUP_EFFECT:
        case WEP_STATE_REPAIR_EFFECT:
            CommonMenuMes1.auto_pos = MES_POS_NONE;
            return;
        case WEP_STATE_WARNING:
        case WEP_STATE_SCRAP_REFUSED:
            DrawWeaponCenteredWarningModal(0x80);
            return;
        default: {
            const int shadow = (WepMenu.mode == WEP_MENU_LIST) ? 1 : 0;

            if (WepMenu.state == WEP_STATE_SCRAP_CONFIRM) {
                DrawWeaponLeftConfirmDialog(0x154, 0x76, 0x190, 0xBE,
                                            LocalizeText("weaponmenu.discard.confirm", "Throw away this weapon?"), 0x80);
            }

            setbilinear(0);
            MenuTextureReload(BtlMenuReadBlock);
            DrawMenuWaku(frame.x, frame.y, frame.width, frame.height, shadow, StayTex, 0x80);
            const int big = (scratch >= 0x80) ? 0 : 1;
            DrawMenuObjectVibe(static_cast<int>(SysCur[0]), static_cast<int>(SysCur[1]), big, scratch);
            return;
        }
    }
}

// =============================================================================
// MAIN BATTLE MENU OVERRIDES
// =============================================================================

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
            // Dungeon (9 items: 0..6 match retail, 7 = Load, 8 = Quit)
            {BTLMENU_PAGE_ITEM, BTLMENU_PAGE_WEAPON, BTLMENU_PAGE_CHARA, BTLMENU_PAGE_ATLA, BTLMENU_PAGE_MOVE,
             BTLMENU_PAGE_OPTION, BTLMENU_PAGE_MANUAL, BTLMENU_PAGE_LOAD, BTLMENU_PAGE_QUIT, BTLMENU_PAGE_NONE},
            // Town (10 items: 0..7 match retail, 8 = Load, 9 = Quit)
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

PORT_OVERRIDE void DrawMenuMove() {
    if (MenuMove.mode == MENU_MOVE_WORLD_MAP_DIRECT) {
        MenuWorldTrans(&MenuCamera);
        FrameImageDraw(0x40, 0x80);
    }

    RefreshBtlMenuTextures();

    int alpha = 0x80;
    if (MenuMove.state == MENU_MOVE_STATE_OPEN) {
        alpha = MenuMove.counter * 6;
    } else if (MenuMove.state == MENU_MOVE_STATE_CLOSE) {
        alpha = 0x80 - MenuMove.counter * 8;
    }
    alpha = std::clamp(alpha, 0, 0x80);

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