#include "menu_manual.hpp"

// Port-specific headers
#include <SDL3/SDL.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
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

static int       tracker_selected_town       = 0;
static bool      tracker_filter_miracle_only = true;
static UI::Cursor s_menu_cursor;

static inline bool IsItemEden(uint16_t id)    { return id == 180; }
static inline bool IsItemGourd(uint16_t id)   { return id == 182; }
static inline bool IsItemPocket(uint16_t id)  { return id == 199; }
static inline bool IsItemDefense(uint16_t id) {
    return (id >= 177 && id <= 179) || (id >= 181 && id <= 184);
}
static inline bool IsItemMiracle(uint16_t id) {
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
};

static TownStats GetTownStats(int town_index) {
    TownStats stats{};
    if (SaveData == nullptr || town_index < 0 || town_index >= TRACKER_TOWN_COUNT) {
        return stats;
    }

    const auto& town = TRACKER_TOWNS[town_index];
    for (int i = 0; i < town.total_chests; ++i) {
        const auto& chest  = town.chests[i];
        bool        opened = (SaveData->GetMapFlag(town.town_id, chest.flag_no) != 0);

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

static TownStats GetGlobalStats() {
    TownStats global{};
    for (int i = 0; i < TRACKER_TOWN_COUNT; ++i) {
        TownStats t = GetTownStats(i);
        global.collected_total   += t.collected_total;
        global.max_total         += t.max_total;
        global.collected_miracle += t.collected_miracle;
        global.max_miracle       += t.max_miracle;
        global.collected_eden    += t.collected_eden;
        global.max_eden          += t.max_eden;
        global.collected_gourd   += t.collected_gourd;
        global.max_gourd         += t.max_gourd;
        global.collected_def     += t.collected_def;
        global.max_def           += t.max_def;
        global.collected_pocket  += t.collected_pocket;
        global.max_pocket        += t.max_pocket;
        global.collected_std     += t.collected_std;
        global.max_std           += t.max_std;
    }
    return global;
}

extern int SetManualMsgBuffer();
extern void ExitManualMenu();
extern void DrawPrevNextCursor();
extern int GetNowManualPartTgaNum();
extern CTexture *ManualMenuTex[6];
extern ClsMes *ManualMsg;

static s16 Port_ManualImgLoad() {
    char path[76];
    int  size;
    if (ReadBGSync() != 0 || ManualMenu.images_ready != 0) BreakReadBG();

    GetPathReadDifferntLang(path);
    strcat(path, "manual/m%d.pac");
    int image_no = ManualMenu.entry + (ManualMenu.category + 1) * 10;
    sprintf(path, path, image_no);
    u_long128 *buffer = ManualMenu.load_buffer;
    buffer = MenuCalcBufAlignment(buffer);
    StartReadBG();
    LoadFileBG(path, buffer, &size);
    buffer += size / 16 + 1;
    buffer = MenuCalcBufAlignment(buffer);

    if ((u32) (image_no - 11) <= 1 || (u32) (image_no - 21) <= 1 || image_no == 31 || image_no == 42) {
        GetPathReadDifferntLang(path);
        strcat(path, "manual/m%db.pac");
        sprintf(path, path, image_no);
        LoadFileBG(path, buffer, &size);
    }
    ReadBG();
    ManualMenu.image_offset = 0;
    ManualMenu.image_reset = 0;
    ManualMenu.images_ready = false;
    return ManualMenu.images_ready;
}

extern s16 ManualImgEnter();

static void Port_DrawManualMsg() {
    if (ManualMenu.messages_ready == 0) return;
    MenuTextureReload(CommonMenuMes3.tex_block);
    
    if (ManualMenu.selection_level < 0) {
        ManualMsg->Step();
    }

    int message_no = 0;
    if (ManualMenu.selection_level == -1) {
        CommonMenuMes3.auto_pos = MES_POS_NONE;
        message_no = ManualMenu.category * 1000 + 1000;
        CommonMenuMes3.stay_frame = true;
        CommonMenuMes3.text_x = 0x86;
        CommonMenuMes3.text_y = 0xEE;
    }

    if (ManualMenu.mode == MANUAL_MODE_READING) {
        CommonMenuMes3.stay_frame = true;
        CommonMenuMes3.auto_pos = MES_POS_BOTTOM;
        message_no = ManualMenu.category * 1000 + 1100 + (ManualMenu.entry + 1) * 10 + ManualMenu.message_page;
        CommonMenuMes3.text_x = 0x78;
        CommonMenuMes3.text_y = 0x158;
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

PORT_OVERRIDE int MenuManualKey() {
    ReadBG();
    int closed = 0;

    switch (ManualMenu.mode) {
        case MANUAL_MODE_LOADING:
            if (SetManualMsgBuffer() != 0) {
                const char* new_list = "Georama\nDungeon\nWeapon\nSavings/Other\nMiracle Chests";
                char* target_buffer = (char*)ManualMenu.common_message_buffer + 0x48;
                strcpy(target_buffer, new_list);

                ManualMenu.mode = MANUAL_MODE_SELECT;
            }
            break;
        case MANUAL_MODE_CLOSING:
            ManualMenu.transition_frame++;
            if (ManualMenu.transition_frame > 32) {
                ExitManualMenu();
                closed = 1;
            }
            break;
        case MANUAL_MODE_SELECT: {
            int  minimum;
            int  maximum;
            int *selection = nullptr;

            if (ManualMenu.selection_level == -2) {
                minimum = 0;
                maximum = 4;
                selection = &ManualMenu.category;
            }

            if (ManualMenu.selection_level == -1) {
                int entry_max[] = {4, 4, 3, 2};
                minimum = 0;
                maximum = entry_max[ManualMenu.category];
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
                    ManualMsg->cursor_row = *selection;
                    CommonMenuMes3.cursor_row = -1;
                } else {
                    CommonMenuMes3.cursor_row = *selection;
                }
            } else {
                ManualMenu.selection_level = -2;
                ManualMenu.entry = 0;
            }

            if (GamePad.Down(PAD_CROSS)) {
                if (ManualMenu.selection_level == -1) {
                    ManualMenu.selection_level++;
                    ManualMenu.mode = MANUAL_MODE_OPENING;
                    ManualMenu.message_page = 0;
                    
                    if (ManualMenu.category != 4) {
                        Port_ManualImgLoad();
                    }
                } else if (ManualMenu.selection_level == -2) {
                    if (ManualMenu.category == 4) {
                        ManualMenu.mode = MANUAL_MODE_TRACKER;
                        tracker_selected_town = 0;
                        s_menu_cursor.SnapTo(58.0f, 168.0f - 2.0f);
                        ComMenuSePlay(MENU_SOUND_CONFIRM);
                    } else {
                        ManualMenu.selection_level++;
                        CommonMenuMes3.cursor_row = 0;
                        ComMenuSePlay(MENU_SOUND_CONFIRM);
                    }
                }
            } else if (GamePad.Down(PAD_CIRCLE)) {
                if (ManualMenu.selection_level < -1) {
                    ManualMenu.mode = MANUAL_MODE_CLOSING;
                    ManualMsg->Preset(MES_PRESET_SYSTEM);
                    CommonMenuMes3.SetBuff(ManualMenu.common_message_buffer);
                    for (int slot = 0; slot < 10; slot++) CommonMenuMes3.mes_no[slot] = -1;
                    CommonMenuMes3.Preset(MES_PRESET_NAME);
                    CommonMenuMes3.char_width = ManualMenu.char_width;
                    ManualMsg->SetBuff(ManualMenu.menu_message_buffer);
                    ManualMsg->Preset(MES_PRESET_SYSTEM);
                } else if (ManualMenu.selection_level == -1) {
                    ManualMenu.selection_level--;
                    *selection = 0;
                }
                ComMenuSePlay(MENU_SOUND_REFUSE);
            }
            break;
        }

        case MANUAL_MODE_TRACKER: {
            if (GamePad.Down(PAD_CIRCLE)) {
                ManualMenu.mode = MANUAL_MODE_SELECT;
                ComMenuSePlay(MENU_SOUND_REFUSE);
            }
            if (GamePad.Down(PAD_UP)) {
                tracker_selected_town--;
                if (tracker_selected_town < 0) tracker_selected_town = TRACKER_TOWN_COUNT - 1;
                ComMenuSePlay(MENU_SOUND_CURSOR);
            }
            if (GamePad.Down(PAD_DOWN)) {
                tracker_selected_town++;
                if (tracker_selected_town >= TRACKER_TOWN_COUNT) tracker_selected_town = 0;
                ComMenuSePlay(MENU_SOUND_CURSOR);
            }
            if (GamePad.Down(PAD_CROSS | PAD_SQUARE)) {
                tracker_filter_miracle_only = !tracker_filter_miracle_only;
                ComMenuSePlay(MENU_SOUND_CONFIRM);
            }
            break;
        }

        case MANUAL_MODE_READING:
            CommonMenuMes3.cursor_row = -1;
            ManualMsg->cursor_row = -1;
            if (GamePad.Down(PAD_R2 | PAD_R1 | PAD_CROSS | PAD_RIGHT)) {
                if (CommonMenuMes3.State() == CLSMES_PAGE_WAIT) {
                    CommonMenuMes3.GoNextPage();
                } else if (CommonMenuMes3.State() == CLSMES_SHOWN) {
                    int image_count = GetNowManualPartTgaNum();
                    ManualMenu.image_page++;
                    ManualMenu.message_page++;
                    if ((ManualMenu.category == 0 && ManualMenu.entry == 4 && ManualMenu.message_page == 1) || 
                        (ManualMenu.category == 1 && ManualMenu.entry == 1 && ManualMenu.message_page == 5) || 
                        (ManualMenu.category == 2 && ManualMenu.entry == 3 && ManualMenu.message_page == 1)) {
                        ManualMenu.image_page--;
                    }
                    if (ManualMenu.image_page >= image_count) {
                        ManualMenu.mode = MANUAL_MODE_SELECT;
                        ManualMenu.selection_level = -1;
                    }
                }
                ComMenuSePlay(MENU_SOUND_CONFIRM);
            } else if (GamePad.Down(PAD_L2 | PAD_L1 | PAD_SQUARE | PAD_LEFT)) {
                if ((ManualMenu.category == 0 && ManualMenu.entry == 4 && ManualMenu.message_page == 1) || 
                    (ManualMenu.category == 1 && ManualMenu.entry == 1 && ManualMenu.message_page == 5) || 
                    (ManualMenu.category == 2 && ManualMenu.entry == 3 && ManualMenu.message_page == 1)) {
                    ManualMenu.message_page--;
                } else if (0 < ManualMenu.image_page) {
                    ManualMenu.message_page--;
                    ManualMenu.image_page--;
                }
                ComMenuSePlay(MENU_SOUND_REFUSE);
            } else if (GamePad.Down(PAD_CIRCLE)) {
                ManualMenu.selection_level = -1;
                ManualMenu.mode = MANUAL_MODE_SELECT;
                ComMenuSePlay(MENU_SOUND_REFUSE);
            }
            break;

        case MANUAL_MODE_OPENING:
            CommonMenuMes3.cursor_row = -1;
            ManualMsg->cursor_row = -1;
            ManualMenu.transition_frame++;
            ManualImgEnter();
            if (ManualMenu.images_ready != 0 && ManualMenu.transition_frame > 32) {
                ManualMenu.mode = MANUAL_MODE_SLIDE_IN;
                ManualMenu.transition_frame = 0;
            }
            break;

        case MANUAL_MODE_SLIDE_IN:
            CommonMenuMes3.cursor_row = -1;
            ManualMsg->cursor_row = -1;
            ManualMenu.transition_frame++;
            if (ManualMenu.transition_frame > 32) {
                ManualMenu.mode = MANUAL_MODE_READING;
            }
            break;
    }
    return closed;
}

PORT_OVERRIDE void MenuManualDraw() {
    
    // --- CATEGORY SELECT MENU ---
    if (ManualMenu.mode == MANUAL_MODE_SELECT) {
        int text_alpha = (ManualMenu.selection_level == -1) ? 0x40 : 0x80;
        int win_alpha  = (ManualMenu.selection_level == -1) ? 0x40 : 0x80;
        
        constexpr int cat_spacing = 28; // +3px vertical row spacing
        UI::TrackerWindow::Draw(135, 85, 220, 170, win_alpha);
        
        if (ManualMenu.selection_level == -2) {
            s_menu_cursor.SetTarget(126.0f, static_cast<float>(100 + (ManualMenu.category * cat_spacing) - 2));
            s_menu_cursor.Draw(0x80);
        }

        MenuTextureReload(0x1A);
        
        std::string categories[] = {
            LocalizeText("manual.category.georama", "Georama"),
            LocalizeText("manual.category.dungeon", "Dungeon"),
            LocalizeText("manual.category.weapon", "Weapon"),
            LocalizeText("manual.category.savings", "Savings/Other"),
            LocalizeText("manual.category.miracle_chests", "Miracle Chests")
        };
        
        for (int i = 0; i < 5; i++) {
            uint32_t color = (i == ManualMenu.category && ManualMenu.selection_level == -2) 
                             ? UI::ColorYellow 
                             : UI::ColorWhite;
            UI::Label(categories[i], color).Draw(160, 100 + (i * cat_spacing), text_alpha);
        }
        
        if (ManualMenu.selection_level == -2) {
            return; 
        }
    }

// --- CHEST TRACKER DASHBOARD ---
    if (ManualMenu.mode == MANUAL_MODE_TRACKER) {
        UI::TrackerWindow::Draw(64, 85, 512, 282, 0x80);
        MenuTextureReload(0x1A);

        constexpr float kHeaderScale = 0.92f;
        constexpr float kBodyScale   = 0.84f;

        constexpr int kLeftPaneX  = 88;
        constexpr int kRightPaneX = 272;
        constexpr int kLabelX     = kRightPaneX + 30; // 24px icon + 6px gap
        constexpr int kCountX     = 460;

        // Header Title
        UI::Label(
            LocalizeText("tracker.title", "JOURNAL: CHEST TRACKER"),
            UI::ColorGold,
            UI::Alignment::Center,
            true,
            1.0f,
            UI::FontWeight::Bold
        ).Draw(320, 96, 0x80);

        // Filter Mode Indicator
        std::string mode_label = tracker_filter_miracle_only
            ? LocalizeText("tracker.filter.miracle_only", "< MIRACLE CHESTS ONLY >")
            : LocalizeText("tracker.filter.all_chests", "< ALL TOWN CHESTS >");
        uint32_t mode_color = tracker_filter_miracle_only ? UI::ColorYellow : UI::ColorWhite;
        UI::Label(mode_label, mode_color, UI::Alignment::Center, true, kBodyScale).Draw(320, 120, 0x80);

        // Left Pane: Settlements List
        UI::Label(
            LocalizeText("tracker.settlements", "SETTLEMENTS"),
            UI::ColorYellow,
            UI::Alignment::Left,
            true,
            kHeaderScale
        ).Draw(kLeftPaneX, 144, 0x80);

        constexpr int left_start_y = 168;
        constexpr int row_spacing  = 26;

        s_menu_cursor.SetTarget(58.0f, static_cast<float>(left_start_y + (tracker_selected_town * row_spacing) - 2));
        s_menu_cursor.Draw(0x80);

        MenuTextureReload(0x1A);
        for (int i = 0; i < TRACKER_TOWN_COUNT; i++) {
            int row_y = left_start_y + (i * row_spacing);
            uint32_t name_color = (i == tracker_selected_town) ? UI::ColorYellow : UI::ColorWhite;
            std::string town_name = LocalizeText(std::string("tracker.town.") + std::to_string(i), TRACKER_TOWNS[i].name);
            UI::Label(town_name, name_color, UI::Alignment::Left, true, kBodyScale).Draw(kLeftPaneX, row_y, 0x80);
        }

        // Left Pane: World Total Section
        TownStats g_stats = GetGlobalStats();
        int g_collected = tracker_filter_miracle_only ? g_stats.collected_miracle : g_stats.collected_total;
        int g_max       = tracker_filter_miracle_only ? g_stats.max_miracle       : g_stats.max_total;

        UI::Label(
            LocalizeText("tracker.world_total", "World Total"),
            UI::ColorGold,
            UI::Alignment::Left,
            true,
            kHeaderScale
        ).Draw(kLeftPaneX, 308, 0x80);

        char g_buf[32];
        snprintf(g_buf, sizeof(g_buf), "[ %2d / %2d ]", g_collected, g_max);
        uint32_t g_color = (g_collected == g_max && g_max > 0) ? UI::ColorGold : UI::ColorWhite;
        UI::Label(g_buf, g_color, UI::Alignment::Left, true, kBodyScale).Draw(kLeftPaneX, 336, 0x80);

        // Right Pane: Breakdown Header
        std::string cur_town_name = LocalizeText(std::string("tracker.town.") + std::to_string(tracker_selected_town), TRACKER_TOWNS[tracker_selected_town].name);
        std::string right_title = LocalizeFormat(LocalizeText("tracker.breakdown", "%1 Breakdown"), {cur_town_name});
        UI::Label(
            right_title,
            UI::ColorYellow,
            UI::Alignment::Left,
            true,
            kHeaderScale
        ).Draw(kRightPaneX, 144, 0x80);

        TownStats cur_stats = GetTownStats(tracker_selected_town);

        // Rotate through the 6 character Defense Treats every 60 frames
        static int s_item_anim_tick = 0;
        constexpr int kDefenseTreatItems[6] = {
            ITEM_FLUFFY_DOUGHNUT, // 136 (Toan)
            ITEM_FISH_CANDY,      // 137 (Xiao)
            ITEM_GRASS_CAKE,      // 138 (Goro)
            ITEM_WITCH_PARFAIT,   // 139 (Ruby)
            ITEM_SCORPION_JERKY,  // 140 (Ungaga)
            ITEM_CARROT_COOKIE    // 141 (Osmond)
        };
        int cur_defense_item = kDefenseTreatItems[(s_item_anim_tick / 60) % 6];
        s_item_anim_tick = (s_item_anim_tick + 1) % 36000;

        struct MetricRow {
            std::string label;
            int         item_no;
            int         collected;
            int         max_val;
        };

        std::vector<MetricRow> breakdown = {
            { LocalizeText("tracker.item.eden", "Fruit of Eden"),     ITEM_FRUIT_OF_EDEN, cur_stats.collected_eden,   cur_stats.max_eden },
            { LocalizeText("tracker.item.gourd", "Gourds"),           ITEM_GOURD,         cur_stats.collected_gourd,  cur_stats.max_gourd },
            { LocalizeText("tracker.item.defense", "Defense Treats"), cur_defense_item,   cur_stats.collected_def,    cur_stats.max_def },
            { LocalizeText("tracker.item.pocket", "Pockets"),         ITEM_POCKET,        cur_stats.collected_pocket, cur_stats.max_pocket }
        };

        if (!tracker_filter_miracle_only) {
            breakdown.push_back({ LocalizeText("tracker.item.standard", "Standard Items"), ITEM_TREASURE_KEY, cur_stats.collected_std, cur_stats.max_std });
        }

        constexpr int right_start_y = 168;
        constexpr int right_spacing = 26;

        for (size_t r = 0; r < breakdown.size(); ++r) {
            int y = right_start_y + (static_cast<int>(r) * right_spacing);

            // 1. Draw 24x24 item icon on the left of the row
            bool icon_drawn = UI::ItemIcon::Draw(breakdown[r].item_no, kRightPaneX, y - 3, 24, 0x80);

            // 2. Draw item name label after the icon (or at pane start if icon texture wasn't available)
            MenuTextureReload(0x1A);
            int text_x = icon_drawn ? kLabelX : kRightPaneX;
            UI::Label(breakdown[r].label, UI::ColorWhite, UI::Alignment::Left, true, kBodyScale).Draw(text_x, y, 0x80);

            // 3. Draw [ collected / max ] count
            char b_buf[32];
            snprintf(b_buf, sizeof(b_buf), "[ %2d / %2d ]", breakdown[r].collected, breakdown[r].max_val);
            uint32_t c_col = (breakdown[r].collected == breakdown[r].max_val && breakdown[r].max_val > 0)
                             ? UI::ColorGold
                             : UI::ColorWhite;
            UI::Label(b_buf, c_col, UI::Alignment::Left, true, kBodyScale).Draw(kCountX, y, 0x80);
        }

        // Right Pane: Area Total (aligned with World Total at Y = 308)
        int town_cur_collected = tracker_filter_miracle_only ? cur_stats.collected_miracle : cur_stats.collected_total;
        int town_cur_max       = tracker_filter_miracle_only ? cur_stats.max_miracle       : cur_stats.max_total;

        constexpr int total_row_y = 308;
        UI::Label(
            LocalizeText("tracker.area_total", "Area Total"),
            UI::ColorYellow,
            UI::Alignment::Left,
            true,
            kHeaderScale
        ).Draw(kRightPaneX, total_row_y, 0x80);

        char area_buf[32];
        snprintf(area_buf, sizeof(area_buf), "[ %2d / %2d ]", town_cur_collected, town_cur_max);
        uint32_t area_col = (town_cur_collected == town_cur_max && town_cur_max > 0)
                            ? UI::ColorGold
                            : UI::ColorWhite;
        UI::Label(area_buf, area_col, UI::Alignment::Left, true, kBodyScale).Draw(kCountX, total_row_y, 0x80);

        // Right Pane: Progress Bar spanning from kRightPaneX (272) to the right edge of the counters (542)
        float progress_ratio = (town_cur_max > 0)
                               ? static_cast<float>(town_cur_collected) / static_cast<float>(town_cur_max)
                               : 0.0f;
        int pct = (town_cur_max > 0) ? (town_cur_collected * 100) / town_cur_max : 0;
        std::string pct_buf = LocalizeFormat(LocalizeText("tracker.progress", "Progress: %1%"), {std::to_string(pct)});

        UI::ProgressBarStyle bar_style{};
        bar_style.text_scale = 0.76f;

        UI::ProgressBar::Draw(
            UI::Rect(kRightPaneX, 335, 270, 24),
            progress_ratio,
            pct_buf,
            bar_style,
            0x80
        );

        // Bottom Controls Help Bar
        MenuTextureReload(0x1A);
        std::string controls_str = LocalizeText(
            "tracker.controls",
            PAD_GLYPH_UP " / " PAD_GLYPH_DOWN " Select   "
            PAD_GLYPH_CROSS " / " PAD_GLYPH_SQUARE " Filter   "
            PAD_GLYPH_CIRCLE " Return"
        );
        UI::Label(
            controls_str,
            UI::ColorWhite,
            UI::Alignment::Center
        ).Draw(320, 418, 0x80);

        return;
    }

    // Original PS2 Image Drawing
    if (ManualMenu.mode >= MANUAL_MODE_OPENING && ManualMenu.mode < MANUAL_MODE_COUNT && ManualMenu.images_ready != 0) {
        MenuTextureReload(ManualMenu.image_texture_block);
        int   image_count = GetNowManualPartTgaNum();
        int   target = -(ManualMenu.image_page * 0x280);
        float distance = target - ManualMenu.image_offset;
        ManualMenu.image_offset += (int) (distance / 4.0f);
        if (std::abs((int) distance) <= 3.0) ManualMenu.image_offset = target;
        int x = ManualMenu.image_offset;

        for (int image = 0; image < image_count; image++, x += 0x280) {
            if (image == 3) MenuTextureReload(ManualMenu.extra_texture_block);
            int source_x = 0;
            int screen_x = x;
            if (x > 0x280) break;
            int width = 0x280;
            MenuTextureClip(screen_x, source_x, width, 0, 0x280);

            if (width > 0x280) width = 0x280;
            if (width < 0) width = 0;

            CRect_i_  screen(screen_x, 0, width, SCREEN_HEIGHT);
            CRect_i_  texel(source_x, 0, width, 0x1C0);
            CTexture *texture = ManualMenuTex[image];
            if (texture != nullptr) {
                DrawMenu2DSprite(texture, screen, texel, 100, 100, 100, 0x80);
            } else {
                int previous = image - 1;
                while (ManualMenuTex[previous] == nullptr) {
                    if (--previous <= 0) break;
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
        if (ManualMenu.mode != MANUAL_MODE_TRACKER) {
            Port_DrawManualMsg();
        }
    }
}