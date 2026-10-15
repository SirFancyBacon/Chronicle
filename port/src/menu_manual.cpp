#include "menu_manual.hpp"

// Port-specific headers
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Retail and engine headers
#include "clsmes.hpp"
#include "dataread.hpp"
#include "dngstatusdata.hpp"
#include "draw2d_port.hpp"
#include "eastking.hpp"
#include "gamepad.hpp"
#include "gametext.hpp"
#include "gameutil.hpp"
#include "itemdata.hpp"
#include "localize.hpp"
#include "memcard.hpp"
#include "menu_draw.hpp"
#include "menuetc.hpp"
#include "mglib.hpp"
#include "rect.hpp"
#include "savedata.hpp"
#include "snd.hpp"
#include "texture.hpp"
#include "userstatus.hpp"

// Chest database & UI framework
#include "chests_data.hpp"
#include "ui_framework.hpp"

#define PAD_GLYPH_UP      "{up}"
#define PAD_GLYPH_DOWN    "{down}"
#define PAD_GLYPH_CROSS   "{cross}"
#define PAD_GLYPH_SQUARE  "{square}"
#define PAD_GLYPH_CIRCLE  "{circle}"

#define MANUAL_MODE_TRACKER 100

extern int       SetManualMsgBuffer();
extern void      ExitManualMenu();
extern void      DrawPrevNextCursor();
extern int       GetNowManualPartTgaNum();
extern s16       ManualImgEnter();
extern CTexture *ManualMenuTex[6];
extern ClsMes   *ManualMsg;

namespace {

int          s_tracker_selected_town       = 0;
bool         s_tracker_filter_miracle_only = true;
UI::Cursor   s_menu_cursor;
UI::Brackets s_menu_brackets;

constexpr bool IsItemEden(uint16_t id)    { return id == 180; }
constexpr bool IsItemGourd(uint16_t id)   { return id == 182; }
constexpr bool IsItemPocket(uint16_t id)  { return id == 199; }
constexpr bool IsItemDefense(uint16_t id) {
    return (id >= 177 && id <= 179) || (id >= 181 && id <= 184);
}
constexpr bool IsItemMiracle(uint16_t id) {
    return IsItemEden(id) || IsItemGourd(id) || IsItemDefense(id) || IsItemPocket(id);
}

struct TownStats {
    int collected_total   = 0;
    int max_total         = 0;
    int collected_miracle = 0;
    int max_miracle       = 0;
    int collected_eden    = 0;
    int max_eden          = 0;
    int collected_gourd   = 0;
    int max_gourd         = 0;
    int collected_def     = 0;
    int max_def           = 0;
    int collected_pocket  = 0;
    int max_pocket        = 0;
    int collected_std     = 0;
    int max_std           = 0;

    void Accumulate(const TownStats& other) {
        collected_total   += other.collected_total;
        max_total         += other.max_total;
        collected_miracle += other.collected_miracle;
        max_miracle       += other.max_miracle;
        collected_eden    += other.collected_eden;
        max_eden          += other.max_eden;
        collected_gourd   += other.collected_gourd;
        max_gourd         += other.max_gourd;
        collected_def     += other.collected_def;
        max_def           += other.max_def;
        collected_pocket  += other.collected_pocket;
        max_pocket        += other.max_pocket;
        collected_std     += other.collected_std;
        max_std           += other.max_std;
    }
};

TownStats GetTownStats(int town_index) {
    TownStats stats{};
    if (SaveData == nullptr || town_index < 0 || town_index >= TRACKER_TOWN_COUNT) {
        return stats;
    }

    const auto& town = TRACKER_TOWNS[town_index];
    for (int i = 0; i < town.total_chests; ++i) {
        const auto& chest  = town.chests[i];
        const bool  opened = (SaveData->GetMapFlag(town.town_id, chest.flag_no) != 0);

        stats.max_total++;
        if (opened) stats.collected_total++;

        if (IsItemEden(chest.item_id)) {
            stats.max_eden++;
            if (opened) stats.collected_eden++;
        } else if (IsItemGourd(chest.item_id)) {
            stats.max_gourd++;
            if (opened) stats.collected_gourd++;
        } else if (IsItemDefense(chest.item_id)) {
            stats.max_def++;
            if (opened) stats.collected_def++;
        } else if (IsItemPocket(chest.item_id)) {
            stats.max_pocket++;
            if (opened) stats.collected_pocket++;
        } else {
            stats.max_std++;
            if (opened) stats.collected_std++;
        }

        if (IsItemMiracle(chest.item_id)) {
            stats.max_miracle++;
            if (opened) stats.collected_miracle++;
        }
    }
    return stats;
}

TownStats GetGlobalStats() {
    TownStats global{};
    for (int i = 0; i < TRACKER_TOWN_COUNT; ++i) {
        global.Accumulate(GetTownStats(i));
    }
    return global;
}

s16 Port_ManualImgLoad() {
    char path[76];
    char format[76];
    int  size = 0;
    if (ReadBGSync() != 0 || ManualMenu.images_ready != 0) {
        BreakReadBG();
    }

    GetPathReadDifferntLang(format);
    std::strcat(format, "manual/m%d.pac");
    const int image_no = ManualMenu.entry + (ManualMenu.category + 1) * 10;
    std::snprintf(path, sizeof(path), format, image_no);

    u_long128 *buffer = MenuCalcBufAlignment(ManualMenu.load_buffer);
    StartReadBG();
    LoadFileBG(path, buffer, &size);
    buffer = MenuCalcBufAlignment(buffer + size / 16 + 1);

    if ((u32)(image_no - 11) <= 1 || (u32)(image_no - 21) <= 1 || image_no == 31 || image_no == 42) {
        GetPathReadDifferntLang(format);
        std::strcat(format, "manual/m%db.pac");
        std::snprintf(path, sizeof(path), format, image_no);
        LoadFileBG(path, buffer, &size);
    }
    ReadBG();
    ManualMenu.image_offset = 0;
    ManualMenu.image_reset  = 0;
    ManualMenu.images_ready = false;
    return ManualMenu.images_ready;
}

void Port_DrawManualMsg() {
    if (ManualMenu.messages_ready == 0) return;
    MenuTextureReload(CommonMenuMes3.tex_block);

    if (ManualMenu.selection_level < 0) {
        ManualMsg->Step();
    }

    int message_no = 0;
    if (ManualMenu.selection_level == -1) {
        CommonMenuMes3.auto_pos   = MES_POS_NONE;
        message_no                = ManualMenu.category * 1000 + 1000;
        CommonMenuMes3.stay_frame = true;
        CommonMenuMes3.text_x     = 0x86;
        CommonMenuMes3.text_y     = 0xEE;
    }

    if (ManualMenu.mode == MANUAL_MODE_READING) {
        CommonMenuMes3.stay_frame = true;
        CommonMenuMes3.auto_pos   = MES_POS_BOTTOM;
        message_no                = ManualMenu.category * 1000 + 1100 + (ManualMenu.entry + 1) * 10 + ManualMenu.message_page;
        CommonMenuMes3.text_x     = 0x78;
        CommonMenuMes3.text_y     = 0x158;
    }

    if (CommonMenuMes3.mes_made != message_no) {
        CommonMenuMes3.MakeMesWin(message_no);
    }
    if (message_no <= 0) {
        CommonMenuMes3.stay_frame = false;
    }

    CommonMenuMes3.Step();
    CommonMenuMes3.DrawMesWin();
}

void DrawCategoryMenu() {
    const int alpha       = (ManualMenu.selection_level == -1) ? 0x40 : 0x80;
    constexpr int spacing = 28;

    UI::TrackerWindow::Draw(135, 85, 220, 170, alpha, UI::BLUE_STONE_MENU);
    MenuTextureReload(0x1A);

    const std::string categories[5] = {
        LocalizeText("manual.category.georama",        "Georama"),
        LocalizeText("manual.category.dungeon",        "Dungeon"),
        LocalizeText("manual.category.weapon",         "Weapon"),
        LocalizeText("manual.category.savings",        "Savings/Other"),
        LocalizeText("manual.category.miracle_chests", "Miracle Chests")
    };

    for (int i = 0; i < 5; i++) {
        const u32 color = (i == ManualMenu.category && ManualMenu.selection_level == -2)
                          ? UI::ColorYellow
                          : UI::ColorWhite;
        UI::Label(categories[i], color).Draw(160, 100 + (i * spacing), alpha);
    }

    if (ManualMenu.selection_level == -2) {
        s_menu_cursor.SetTarget(126.0f, static_cast<float>(100 + (ManualMenu.category * spacing) - 2));
        s_menu_cursor.Draw(0x80);
    }
}

void DrawChestTracker() {
    constexpr float kHeaderScale = 0.92f;
    constexpr float kBodyScale   = 0.84f;

    constexpr int kLeftPaneX  = 92;
    constexpr int kDividerX   = 258;
    constexpr int kRightPaneX = 272;
    constexpr int kLabelX     = kRightPaneX + 30;
    constexpr int kCountX     = 460;

    // 1. Paper Menu Window lowered 15px (Y = 76 -> 91, height = 262, bottom = 353)
    //    (BlueStone remains available via UI::BLUE_STONE_MENU for other UI elements)
    UI::TrackerWindow::DrawPaper(64, 91, 512, 262, 0x80);

    // 2. Parchment Scroll Title Banner crowning the top edge
    UI::TrackerWindow::DrawStoneBanner(
        UI::Rect(150, 74, 340, 28),
        0x80,
        0x80,
        LocalizeText("tracker.title", "JOURNAL: CHEST TRACKER")
    );

    // 3. Chiseled Divider Lines (lowered 15px with the background)
    UI::Divider::DrawVertical(kDividerX, 133, 337, 0x80);
    UI::Divider::DrawHorizontal(84, kDividerX - 10, 283, 0x80);
    UI::Divider::DrawHorizontal(kRightPaneX, 542, 283, 0x80);

    MenuTextureReload(0x1A);

    // 4. Filter Mode Indicator
    const std::string mode_label = s_tracker_filter_miracle_only
        ? LocalizeText("tracker.filter.stat_boosts_only", "< STAT BOOSTS ONLY >")
        : LocalizeText("tracker.filter.all_chests",       "< ALL TOWN CHESTS >");
    const u32 mode_color = s_tracker_filter_miracle_only ? UI::ColorYellow : UI::ColorWhite;
    UI::Label(mode_label, mode_color, UI::Alignment::Center, true, kBodyScale).Draw(320, 109, 0x80);

    // 5. Left Pane: Settlements List
    UI::Label(LocalizeText("tracker.settlements", "SETTLEMENTS"),
              UI::ColorYellow, UI::Alignment::Left, true, kHeaderScale).Draw(kLeftPaneX, 135, 0x80);

    constexpr int left_start_y = 159;
    constexpr int row_spacing  = 24;

    for (int i = 0; i < TRACKER_TOWN_COUNT; i++) {
        const int  row_y     = left_start_y + (i * row_spacing);
        const bool selected  = (i == s_tracker_selected_town);
        const u32  color     = selected ? UI::ColorYellow : UI::ColorWhite;
        const int  row_alpha = selected ? 0x80 : 0x58;

        std::string town_name = LocalizeText("tracker.town." + std::to_string(i), TRACKER_TOWNS[i].name);
        UI::Label(town_name, color, UI::Alignment::Left, true, kBodyScale).Draw(kLeftPaneX, row_y, row_alpha);
    }

    // 6. Left Pane: World Total
    const TownStats g_stats     = GetGlobalStats();
    const int       g_collected = s_tracker_filter_miracle_only ? g_stats.collected_miracle : g_stats.collected_total;
    const int       g_max       = s_tracker_filter_miracle_only ? g_stats.max_miracle       : g_stats.max_total;

    UI::Label(LocalizeText("tracker.world_total", "World Total"),
              UI::ColorGold, UI::Alignment::Left, true, kHeaderScale).Draw(kLeftPaneX, 291, 0x80);
    UI::DrawCounter(kLeftPaneX, 316, g_collected, g_max, kBodyScale);

    // 7. Right Pane: Selected Settlement Breakdown
    std::string cur_town_name = LocalizeText("tracker.town." + std::to_string(s_tracker_selected_town),
                                             TRACKER_TOWNS[s_tracker_selected_town].name);
    std::string right_title   = LocalizeFormat(LocalizeText("tracker.breakdown", "%1 Breakdown"), {cur_town_name});
    UI::Label(right_title, UI::ColorYellow, UI::Alignment::Left, true, kHeaderScale).Draw(kRightPaneX, 135, 0x80);

    const TownStats cur_stats = GetTownStats(s_tracker_selected_town);

    static int s_item_anim_tick = 0;
    constexpr int kDefenseTreatItems[6] = {
        ITEM_FLUFFY_DOUGHNUT, ITEM_FISH_CANDY,      ITEM_GRASS_CAKE,
        ITEM_WITCH_PARFAIT,   ITEM_SCORPION_JERKY,  ITEM_CARROT_COOKIE
    };
    const int cur_defense_item = kDefenseTreatItems[(s_item_anim_tick / 60) % 6];
    s_item_anim_tick           = (s_item_anim_tick + 1) % 36000;

    struct MetricRow {
        std::string label;
        int         item_no;
        int         collected;
        int         max_val;
    };

    std::vector<MetricRow> breakdown = {
        {LocalizeText("tracker.item.eden",    "Fruit of Eden"),  ITEM_FRUIT_OF_EDEN, cur_stats.collected_eden,   cur_stats.max_eden},
        {LocalizeText("tracker.item.gourd",   "Gourds"),         ITEM_GOURD,         cur_stats.collected_gourd,  cur_stats.max_gourd},
        {LocalizeText("tracker.item.defense", "Defense Treats"), cur_defense_item,   cur_stats.collected_def,    cur_stats.max_def},
        {LocalizeText("tracker.item.pocket",  "Pockets"),        ITEM_POCKET,        cur_stats.collected_pocket, cur_stats.max_pocket}
    };

    if (!s_tracker_filter_miracle_only) {
        breakdown.push_back({LocalizeText("tracker.item.standard", "Standard Items"), ITEM_TREASURE_KEY, cur_stats.collected_std, cur_stats.max_std});
    }

    constexpr int right_start_y = 159;
    constexpr int right_spacing = 24;

    for (size_t r = 0; r < breakdown.size(); ++r) {
        const int  y          = right_start_y + (static_cast<int>(r) * right_spacing);
        const bool icon_drawn = UI::ItemIcon::Draw(breakdown[r].item_no, kRightPaneX, y - 3, 24, 0x80);

        MenuTextureReload(0x1A);
        const int text_x = icon_drawn ? kLabelX : kRightPaneX;
        UI::Label(breakdown[r].label, UI::ColorWhite, UI::Alignment::Left, true, kBodyScale).Draw(text_x, y, 0x80);
        UI::DrawCounter(kCountX, y, breakdown[r].collected, breakdown[r].max_val, kBodyScale);
    }

    // 8. Right Pane: Area Total & Progress Bar (centered text)
    const int town_cur_collected = s_tracker_filter_miracle_only ? cur_stats.collected_miracle : cur_stats.collected_total;
    const int town_cur_max       = s_tracker_filter_miracle_only ? cur_stats.max_miracle       : cur_stats.max_total;

    constexpr int total_row_y = 291;
    UI::Label(LocalizeText("tracker.area_total", "Area Total"),
              UI::ColorYellow, UI::Alignment::Left, true, kHeaderScale).Draw(kRightPaneX, total_row_y, 0x80);
    UI::DrawCounter(kCountX, total_row_y, town_cur_collected, town_cur_max, kBodyScale);

    const float progress_ratio = (town_cur_max > 0)
                                 ? static_cast<float>(town_cur_collected) / static_cast<float>(town_cur_max)
                                 : 0.0f;
    const int   pct            = (town_cur_max > 0) ? (town_cur_collected * 100) / town_cur_max : 0;
    std::string pct_buf        = LocalizeFormat(LocalizeText("tracker.progress", "Progress: %1%"), {std::to_string(pct)});

    UI::ProgressBarStyle bar_style{};
    bar_style.text_scale = 0.76f;
    UI::ProgressBar::Draw(UI::Rect(kRightPaneX, 314, 270, 22), progress_ratio, pct_buf, bar_style, 0x80);

    // 9. Bottom Controls Bar & Selection Cursor
    MenuTextureReload(0x1A);
    std::string controls_str = LocalizeText(
        "tracker.controls",
        PAD_GLYPH_UP " / " PAD_GLYPH_DOWN " Select   "
        PAD_GLYPH_CROSS " / " PAD_GLYPH_SQUARE " Filter   "
        PAD_GLYPH_CIRCLE " Return"
    );
    UI::Label(controls_str, UI::ColorWhite, UI::Alignment::Center).Draw(320, 418, 0x80);

    const int sel_y = left_start_y + (s_tracker_selected_town * row_spacing);
    s_menu_brackets.Draw(UI::Rect(kLeftPaneX - 4, sel_y - 1, 152, 22), 0x80);

    s_menu_cursor.SetTarget(48.0f, static_cast<float>(sel_y - 2));
    s_menu_cursor.Draw(0x80);
}

bool IsMultiPageManualEntry() {
    return (ManualMenu.category == 0 && ManualMenu.entry == 4 && ManualMenu.message_page == 1) ||
           (ManualMenu.category == 1 && ManualMenu.entry == 1 && ManualMenu.message_page == 5) ||
           (ManualMenu.category == 2 && ManualMenu.entry == 3 && ManualMenu.message_page == 1);
}

} // namespace

PORT_OVERRIDE int MenuManualKey() {
    ReadBG();
    int closed = 0;

    switch (ManualMenu.mode) {
        case MANUAL_MODE_LOADING:
            if (SetManualMsgBuffer() != 0) {
                const char* new_list      = "Georama\nDungeon\nWeapon\nSavings/Other\nMiracle Chests";
                char*       target_buffer = reinterpret_cast<char*>(ManualMenu.common_message_buffer) + 0x48;
                std::strcpy(target_buffer, new_list);
                ManualMenu.mode = MANUAL_MODE_SELECT;
            }
            break;

        case MANUAL_MODE_CLOSING:
            if (++ManualMenu.transition_frame > 32) {
                ExitManualMenu();
                closed = 1;
            }
            break;

        case MANUAL_MODE_SELECT: {
            int  minimum   = 0;
            int  maximum   = 0;
            int *selection = nullptr;

            if (ManualMenu.selection_level == -2) {
                maximum   = 4;
                selection = &ManualMenu.category;
            } else if (ManualMenu.selection_level == -1) {
                constexpr int kEntryMax[] = {4, 4, 3, 2};
                maximum   = kEntryMax[ManualMenu.category];
                selection = &ManualMenu.entry;
            }

            if (selection != nullptr) {
                if (GamePad.Down(PAD_UP)) {
                    (*selection)--;
                    ComMenuSePlay(MENU_SOUND_CURSOR);
                }
                if (GamePad.Down(PAD_DOWN)) {
                    (*selection)++;
                    ComMenuSePlay(MENU_SOUND_CURSOR);
                }
                if (*selection < minimum) *selection = maximum;
                if (*selection > maximum) *selection = minimum;

                if (ManualMenu.selection_level == -2) {
                    ManualMsg->cursor_row     = *selection;
                    CommonMenuMes3.cursor_row = -1;
                } else {
                    CommonMenuMes3.cursor_row = *selection;
                }
            } else {
                ManualMenu.selection_level = -2;
                ManualMenu.entry           = 0;
            }

            if (GamePad.Down(PAD_CROSS)) {
                if (ManualMenu.selection_level == -1) {
                    ManualMenu.selection_level++;
                    ManualMenu.mode         = MANUAL_MODE_OPENING;
                    ManualMenu.message_page = 0;
                    if (ManualMenu.category != 4) {
                        Port_ManualImgLoad();
                    }
                } else if (ManualMenu.selection_level == -2) {
                    if (ManualMenu.category == 4) {
                        ManualMenu.mode         = MANUAL_MODE_TRACKER;
                        s_tracker_selected_town = 0;
                        s_menu_cursor.SnapTo(48.0f, 159.0f - 2.0f);
                    } else {
                        ManualMenu.selection_level++;
                        CommonMenuMes3.cursor_row = 0;
                    }
                    ComMenuSePlay(MENU_SOUND_CONFIRM);
                }
            } else if (GamePad.Down(PAD_CIRCLE)) {
                if (ManualMenu.selection_level < -1) {
                    ManualMenu.mode = MANUAL_MODE_CLOSING;
                    ManualMsg->Preset(MES_PRESET_SYSTEM);
                    CommonMenuMes3.SetBuff(ManualMenu.common_message_buffer);
                    for (int slot = 0; slot < 10; slot++) {
                        CommonMenuMes3.mes_no[slot] = -1;
                    }
                    CommonMenuMes3.Preset(MES_PRESET_NAME);
                    CommonMenuMes3.char_width = ManualMenu.char_width;
                    ManualMsg->SetBuff(ManualMenu.menu_message_buffer);
                    ManualMsg->Preset(MES_PRESET_SYSTEM);
                } else if (ManualMenu.selection_level == -1 && selection != nullptr) {
                    ManualMenu.selection_level--;
                    *selection = 0;
                }
                ComMenuSePlay(MENU_SOUND_REFUSE);
            }
            break;
        }

        case MANUAL_MODE_TRACKER:
            if (GamePad.Down(PAD_CIRCLE)) {
                ManualMenu.mode = MANUAL_MODE_SELECT;
                ComMenuSePlay(MENU_SOUND_REFUSE);
            }
            if (GamePad.Down(PAD_UP)) {
                if (--s_tracker_selected_town < 0) s_tracker_selected_town = TRACKER_TOWN_COUNT - 1;
                ComMenuSePlay(MENU_SOUND_CURSOR);
            }
            if (GamePad.Down(PAD_DOWN)) {
                if (++s_tracker_selected_town >= TRACKER_TOWN_COUNT) s_tracker_selected_town = 0;
                ComMenuSePlay(MENU_SOUND_CURSOR);
            }
            if (GamePad.Down(PAD_CROSS | PAD_SQUARE)) {
                s_tracker_filter_miracle_only = !s_tracker_filter_miracle_only;
                ComMenuSePlay(MENU_SOUND_CONFIRM);
            }
            break;

        case MANUAL_MODE_READING:
            CommonMenuMes3.cursor_row = -1;
            ManualMsg->cursor_row     = -1;
            if (GamePad.Down(PAD_R2 | PAD_R1 | PAD_CROSS | PAD_RIGHT)) {
                if (CommonMenuMes3.State() == CLSMES_PAGE_WAIT) {
                    CommonMenuMes3.GoNextPage();
                } else if (CommonMenuMes3.State() == CLSMES_SHOWN) {
                    const int image_count = GetNowManualPartTgaNum();
                    ManualMenu.image_page++;
                    ManualMenu.message_page++;
                    if (IsMultiPageManualEntry()) {
                        ManualMenu.image_page--;
                    }
                    if (ManualMenu.image_page >= image_count) {
                        ManualMenu.mode            = MANUAL_MODE_SELECT;
                        ManualMenu.selection_level = -1;
                    }
                }
                ComMenuSePlay(MENU_SOUND_CONFIRM);
            } else if (GamePad.Down(PAD_L2 | PAD_L1 | PAD_SQUARE | PAD_LEFT)) {
                if (IsMultiPageManualEntry()) {
                    ManualMenu.message_page--;
                } else if (ManualMenu.image_page > 0) {
                    ManualMenu.message_page--;
                    ManualMenu.image_page--;
                }
                ComMenuSePlay(MENU_SOUND_REFUSE);
            } else if (GamePad.Down(PAD_CIRCLE)) {
                ManualMenu.selection_level = -1;
                ManualMenu.mode            = MANUAL_MODE_SELECT;
                ComMenuSePlay(MENU_SOUND_REFUSE);
            }
            break;

        case MANUAL_MODE_OPENING:
            CommonMenuMes3.cursor_row = -1;
            ManualMsg->cursor_row     = -1;
            ManualMenu.transition_frame++;
            ManualImgEnter();
            if (ManualMenu.images_ready != 0 && ManualMenu.transition_frame > 32) {
                ManualMenu.mode             = MANUAL_MODE_SLIDE_IN;
                ManualMenu.transition_frame = 0;
            }
            break;

        case MANUAL_MODE_SLIDE_IN:
            CommonMenuMes3.cursor_row = -1;
            ManualMsg->cursor_row     = -1;
            if (++ManualMenu.transition_frame > 32) {
                ManualMenu.mode = MANUAL_MODE_READING;
            }
            break;
    }
    return closed;
}

PORT_OVERRIDE void MenuManualDraw() {
    if (ManualMenu.mode == MANUAL_MODE_SELECT) {
        DrawCategoryMenu();
        if (ManualMenu.selection_level == -2) {
            return;
        }
    }

    if (ManualMenu.mode == MANUAL_MODE_TRACKER) {
        DrawChestTracker();
        return;
    }

    if (ManualMenu.mode >= MANUAL_MODE_OPENING && ManualMenu.mode < MANUAL_MODE_COUNT && ManualMenu.images_ready != 0) {
        MenuTextureReload(ManualMenu.image_texture_block);
        const int   image_count = GetNowManualPartTgaNum();
        const int   target      = -(ManualMenu.image_page * 0x280);
        const float distance    = static_cast<float>(target - ManualMenu.image_offset);
        ManualMenu.image_offset += static_cast<int>(distance / 4.0f);
        if (std::abs(static_cast<int>(distance)) <= 3) {
            ManualMenu.image_offset = target;
        }

        int x = ManualMenu.image_offset;
        for (int image = 0; image < image_count; image++, x += 0x280) {
            if (image == 3) MenuTextureReload(ManualMenu.extra_texture_block);
            if (x > 0x280) break;

            int source_x = 0;
            int screen_x = x;
            int width    = 0x280;
            MenuTextureClip(screen_x, source_x, width, 0, 0x280);
            width = std::clamp(width, 0, 0x280);

            CRect_i_  screen(screen_x, 0, width, SCREEN_HEIGHT);
            CRect_i_  texel(source_x, 0, width, 0x1C0);
            CTexture *texture = ManualMenuTex[image];
            if (texture != nullptr) {
                DrawMenu2DSprite(texture, screen, texel, 100, 100, 100, 0x80);
            } else {
                int previous = image - 1;
                while (previous > 0 && ManualMenuTex[previous] == nullptr) {
                    --previous;
                }
                if (previous >= 0) {
                    DrawMenu2DSprite(texture, screen, texel, 0x50, 0x50, 0x50, 0x80);
                }
            }
        }
        MenuTextureReload(ManualMenu.common_texture_block);
        DrawPrevNextCursor();
    }

    if (ManualMenu.messages_ready != 0 && ManualMenu.mode != MANUAL_MODE_CLOSING) {
        Port_DrawManualMsg();
    }
}

std::vector<std::pair<std::string, std::string>> UI::JournalStrings() {
    std::vector<std::pair<std::string, std::string>> out = {
        {"manual.category.georama",         "Georama"                                                        },
        {"manual.category.dungeon",         "Dungeon"                                                        },
        {"manual.category.weapon",          "Weapon"                                                         },
        {"manual.category.savings",         "Savings/Other"                                                  },
        {"manual.category.miracle_chests",  "Miracle Chests"                                                 },
        {"tracker.title",                   "JOURNAL: CHEST TRACKER"                                         },
        {"tracker.filter.stat_boosts_only", "< STAT BOOSTS ONLY >"                                           },
        {"tracker.filter.all_chests",       "< ALL TOWN CHESTS >"                                            },
        {"tracker.settlements",             "SETTLEMENTS"                                                    },
        {"tracker.world_total",             "World Total"                                                    },
        {"tracker.breakdown",               "%1 Breakdown"                                                   },
        {"tracker.item.eden",               "Fruit of Eden"                                                  },
        {"tracker.item.gourd",              "Gourds"                                                         },
        {"tracker.item.defense",            "Defense Treats"                                                 },
        {"tracker.item.pocket",             "Pockets"                                                        },
        {"tracker.item.standard",           "Standard Items"                                                 },
        {"tracker.progress",                "Progress: %1%"                                                  },
        {"tracker.area_total",              "Area Total"                                                     },
        {"tracker.controls",                PAD_GLYPH_UP " / " PAD_GLYPH_DOWN " Select   " PAD_GLYPH_CROSS " / " PAD_GLYPH_SQUARE
                                          " Filter   " PAD_GLYPH_CIRCLE " Return"},
    };
    for (size_t i = 0; i < std::size(TRACKER_TOWNS); i++) {
        out.emplace_back("tracker.town." + std::to_string(i), TRACKER_TOWNS[i].name);
    }
    return out;
}
