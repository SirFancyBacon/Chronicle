#include "menu_manual.hpp"

// Port-specific headers
#include <SDL3/SDL.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>

// Retail headers
#include "clsmes.hpp"
#include "dataread.hpp"
#include "dngstatusdata.hpp"
#include "eastking.hpp"
#include "gamepad.hpp"
#include "memcard.hpp"
#include "menu_draw.hpp"
#include "menuetc.hpp"
#include "mglib.hpp"
#include "rect.hpp"
#include "savedata.hpp"
#include "snd.hpp"
#include "texture.hpp"
#include "userstatus.hpp"
#include "gametext.hpp"
#include "gameutil.hpp"

// Native Color Palette Enums (Required for Drop Shadows)
constexpr u32 NATIVE_COLOR_WHITE  = FONT_COLOR_WHITE;
constexpr u32 NATIVE_COLOR_GOLD   = FONT_COLOR_GOLD;
constexpr u32 NATIVE_COLOR_YELLOW = FONT_COLOR_YELLOW;

// ==============================================================
// MOD UI FRAMEWORK
// ==============================================================
namespace ModUI {

    class DynamicText {
    public:
        GameText text;
        u32 color;
        DynamicText(const char* string, u32 c = NATIVE_COLOR_WHITE) : color(c) {
            text.Set(string);
        }
        void Draw(int x, int y, int alpha = 0x80) {
            text.SetColour(color); 
            text.RefreshGlyphs();
            text.Draw(x, y, alpha);
        }
        int GetWidth() {
            text.RefreshGlyphs();
            return text.Width();
        }
    };

    void DrawWindow(int x, int y, int width_px, int height_px, int alpha = 0x80) {
        // Uses your verified custom block height calculation
        float w = (width_px - 28) / 16.0f;
        float h = (height_px - 76) / 16.0f;
        if (w < 0.0f) w = 0.0f;
        if (h < 0.0f) h = 0.0f;
        MenuHelpWinDraw(x, y, w, h, alpha);
    }

} // namespace ModUI

// ==============================================================
// TRACKER DATA & STATE
// ==============================================================

#define MANUAL_MODE_TRACKER 100
static int tracker_selected_town = 0;

struct TrackerTown {
    const char* name;
    int max_chests;
    int flag_start; 
};

static TrackerTown tracker_data[] = {
    {"Norune Village", 18, 100},
    {"Matataki Village", 15, 200},
    {"Queens", 15, 300},
    {"Muska Lacka", 15, 400},
    {"Yellow Drops", 15, 500}
};

const int TRACKER_TOWN_COUNT = 5;

// Externs mapped from retail menu_manual.cpp
extern int SetManualMsgBuffer();
extern void ExitManualMenu();
extern void DrawPrevNextCursor();
extern int GetNowManualPartTgaNum();
extern CTexture *ManualMenuTex[6];
extern ClsMes *ManualMsg;

// Local PS2 Image Loader Bypass
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

// ==============================================================
// NATIVE PS2 TEXT OVERRIDE
// ==============================================================
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

// ==============================================================
// MENU MANUAL OVERRIDE
// ==============================================================
PORT_OVERRIDE int MenuManualKey() {
    ReadBG();
    int closed = 0;

    switch (ManualMenu.mode) {
        case MANUAL_MODE_LOADING:
            if (SetManualMsgBuffer() != 0) {
                // String Intercept for 5th row
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
            int *selection = NULL;

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

            if (selection != NULL) {
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
    
    // --- NATIVE CATEGORY SELECT MENU ---
    if (ManualMenu.mode == MANUAL_MODE_SELECT) {
        int text_alpha = (ManualMenu.selection_level == -1) ? 128 : 255;
        int win_alpha  = (ManualMenu.selection_level == -1) ? 0x40 : 0x80;
        
        ModUI::DrawWindow(135, 85, 220, 160, win_alpha);
        
        MenuTextureReload(0x1A);
        
        const char* categories[] = {
            "Georama", "Dungeon", "Weapon", "Savings/Other", "Miracle Chests"
        };
        
        int start_y = 100;
        for (int i = 0; i < 5; i++) {
            int row_y = start_y + (i * 25);
            u32 color = (i == ManualMenu.category) ? NATIVE_COLOR_YELLOW : NATIVE_COLOR_WHITE;
            
            if (ManualMenu.selection_level == -1 && i != ManualMenu.category) {
                color = NATIVE_COLOR_WHITE;
            }
            
            ModUI::DynamicText text(categories[i], color);
            text.Draw(160, row_y, text_alpha); 
            
            if (i == ManualMenu.category && ManualMenu.selection_level == -2) {
                ModUI::DynamicText cursor(">", NATIVE_COLOR_YELLOW);
                cursor.Draw(145, row_y, 255); 
            }
        }
        
        if (ManualMenu.selection_level == -2) {
            return; 
        }
    }

    // --- NATIVE TRACKER DRAW LOOP ---
    if (ManualMenu.mode == MANUAL_MODE_TRACKER) {
        ModUI::DrawWindow(64, 40, 512, 360, 0x80); 
        
        MenuTextureReload(0x1A);
        
        ModUI::DynamicText header("JOURNAL: MIRACLE CHESTS", NATIVE_COLOR_YELLOW);
        header.Draw(320 - (header.GetWidth() / 2), 60, 255); 
        
        int start_y = 120;
        int row_spacing = 40;
        
        for (int i = 0; i < TRACKER_TOWN_COUNT; i++) {
            int row_y = start_y + (i * row_spacing);
            
            u32 color = (i == tracker_selected_town) ? NATIVE_COLOR_YELLOW : NATIVE_COLOR_WHITE;
            ModUI::DynamicText town_name(tracker_data[i].name, color);
            town_name.Draw(120, row_y, 255);
            
            char progress_buf[32];
            sprintf(progress_buf, "[ 0 / %d ]", tracker_data[i].max_chests);
            ModUI::DynamicText progress(progress_buf, NATIVE_COLOR_WHITE);
            progress.Draw(450, row_y, 255);
            
            if (i == tracker_selected_town) {
                ModUI::DynamicText cursor(">", NATIVE_COLOR_YELLOW);
                cursor.Draw(100, row_y, 255);
            }
        }
        return; 
    }

    // --- Original PS2 Image Drawing ---
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
            if (texture != NULL) {
                DrawMenu2DSprite(texture, screen, texel, 100, 100, 100, 0x80);
            } else {
                int previous = image - 1;
                while (ManualMenuTex[previous] == NULL) {
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

    // --- Sub-Menus and Tutorial Box ---
    if (ManualMenu.messages_ready != 0 && ManualMenu.mode != MANUAL_MODE_CLOSING) {
        if (ManualMenu.mode != MANUAL_MODE_TRACKER) {
            Port_DrawManualMsg();
        }
    }
}