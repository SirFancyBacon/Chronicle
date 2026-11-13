#include "langset.hpp"

#include <libpkt.h>

#include <array>

#include "dataalloc.hpp"
#include "dataread.hpp"
#include "dataset.hpp"
#include "fader.hpp"
#include "gamepad.hpp"
#include "mainselect.hpp"
#include "mglib.hpp"
#include "rect.hpp"
#include "snd.hpp"
#include "texture.hpp"
#include "ui/framework.hpp"
#include "ui/menu_pointer.hpp"
#include "vutext.hpp"

namespace dc::game {

Fader Fade;
int   Cursor;
int   Proc;

namespace {

constexpr int kLanguageCount = 5;

// Retail's plate, lang_set: its heading, then a band for each language, each drawn where it lies in the picture, so screen and texels are one place.
constexpr ui::Rect kHeading = {0x9E, 0, 0x144, 0x5A};

// Unchosen languages are drawn at half, the chosen one at full, as retail's.
constexpr int kDim = 0x40;
constexpr int kLit = 0x80;

// Where a language's band lies, on the screen and in the picture: what's drawn there, and what the mouse finds there.
ui::Rect Entry(int language) {
    constexpr std::array<int, kLanguageCount> tops{0x5F, 0x9C, 0xD9, 0x116, 0x153};
    return {0x9E, tops[static_cast<std::size_t>(language)], 0x144, 0x3C};
}

// The language whose band holds the point, or -1.
int EntryAt(float x, float y) {
    for (int language = 0; language < kLanguageCount; language++) {
        if (Entry(language).Contains(x, y)) {
            return language;
        }
    }
    return -1;
}

void DrawBand(const ui::Rect &band, int opacity) {
    ui::Picture::Draw(TexManager.GetTexture("lang_set", -1), band, band, Fade.Get(opacity));
}

// The mouse, while the screen is choosing: a hover moves the cursor to the language under it, a left click chooses it, the wheel moves the cursor as Up
// and Down do. Retail's screen plays no sound, and nor does this. The pointer is the title's hand, which follows the mouse while it's in use; with the
// pad, the screen looks as retail's.
ui::MenuPointer g_pointer;

// Retail's title hand (icon01), the same picture in every language's title pack; this screen comes before a language, and so before a title pack, is
// chosen, and the screen only shows with PAL's several languages, so it's taken from the English one. Without it the mouse still works, unseen.
constexpr ui::Rect kHand = {0, 0, 32, 32};
CTexture         *g_hand = nullptr;
// Where the hand was last drawn, while the mouse was in use: it fades out from there with the screen once a language is chosen.
bool              g_hand_shown = false;
float             g_hand_x = 0.0f;
float             g_hand_y = 0.0f;

void LoadHand() {
    g_hand = nullptr;
    g_hand_shown = false;
    char pack_path[] = "titledat/title_eu.pak";
    char hand_name[] = "icon01.img";
    if (LoadFile2(pack_path, read_buffer, nullptr, 0) == 0) {
        return;
    }
    LOADTEXTURE_INFO2 textures[] = {
        {reinterpret_cast<char *>(GetPackFile(read_buffer, hand_name, nullptr)), 1, 0},
        {nullptr,                                                               1, 0}
    };
    if (textures[0].name == nullptr) {
        return;
    }
    TexManager.LoadTextureBlock(-1, textures);
    g_hand = TexManager.GetTexture("icon01", -1);
}

// Reports that the mouse chose the language under it.
bool PointerChose() {
    if (!g_pointer.IsOpen()) {
        g_pointer.Open();
    }
    ui::MenuPointerShow(&g_pointer);
    const ui::Rect                  at = Entry(Cursor);
    const ui::MenuPointerTake take = g_pointer.Take(static_cast<float>(at.x + at.w / 2), static_cast<float>(at.y + at.h / 2));
    if (!take.pointing) {
        return false;
    }
    if (take.wheel != 0.0f) {
        Cursor = (Cursor + (take.wheel > 0.0f ? kLanguageCount - 1 : 1)) % kLanguageCount;
        return false;
    }
    const int under = EntryAt(g_pointer.x, g_pointer.y);
    if (under < 0 || !take.moved) {
        return false;
    }
    Cursor = under;
    return (take.clicked & 1) != 0;
}

} // namespace

void LangsetInit() {
    InitializeDataBuffer();
    SetDataBuffer(&VisualData, 200000);
    SetDataBuffer(&MotionData, 500000);
    SetDataBuffer(&TextureData, 300000);
    SetPacketReadBuffer(40000, 300000);
    MGSetBGColor(0.0f, 0.0f, 0.0f, 128.0f);
    std::array<LOADTEXTURE_INFO, 5> textures{
        {
         {.name = "#frame_image_mes#640#" SCREEN_HEIGHT_STR "#4", .block_no = 26, .mipmap = 0},
         {.name = "#fukidashibase#640#" HALF_BUFFER_HEIGHT_STR "#4", .block_no = 26, .mipmap = 0},
         {.name = "#fontbase#512#256#1", .block_no = 26, .mipmap = 0},
         {.name = "titledat/lang_set.img", .block_no = 0, .mipmap = 0},
         {.name = "", .block_no = 0, .mipmap = 0},
         }
    };
    TexManager.Initialize(0x3FE0);
    TexManager.LoadTextureBlock(-1, textures.data(), read_buffer);
    LoadHand();
    GamePad.SetAutoRepeat(PAD_UP | PAD_DOWN, 30, 9);
    GamePad.MenuModeOn(120);
    Fade.value = 0;
    Cursor = 0;
    Proc = LANGSET_FADE_IN;
}

int LangsetLoop() {
    switch (Proc) {
        case LANGSET_FADE_IN:
            if (Fade.In() != 0) {
                Proc = LANGSET_SELECT;
            }

            break;
        case LANGSET_SELECT:
            if (LangsetProc() != 0) {
                Proc = LANGSET_FADE_OUT;
            }

            break;
        case LANGSET_FADE_OUT:
            // The mouse goes back to the pad only once none of its buttons is held, so a click held through the fade isn't a press of what comes next:
            // the screen stays dark until it's let go.
            if (Fade.Out() != 0 && g_pointer.Close()) {
                ui::MenuPointerShow(nullptr);
                // The first two codes are not offered here, so the cursor counts from the third.
                LanguageCode = Cursor + 2;
                return 1;
            }

            break;
        default:
            break;
    }

    LangsetDraw();
    return 0;
}

int LangsetProc() {
    if (PointerChose()) {
        return 1;
    }

    if (GamePad.Down(PAD_UP) != 0) {
        Cursor--;
    }

    if (GamePad.Down(PAD_DOWN) != 0) {
        Cursor++;
    }

    if (Cursor < 0) {
        Cursor = kLanguageCount - 1;
    }

    if (Cursor > kLanguageCount - 1) {
        Cursor = 0;
    }

    if (GamePad.Down(PAD_START) != 0 || GamePad.Down(PAD_CROSS) != 0) {
        return 1;
    }

    return 0;
}

void LangsetDraw() {
    setbilinear(1);
    TexManager.ReloadTexture(GetVif1Packet(), 0);
    DrawBand(kHeading, kLit);
    for (int language = 0; language < kLanguageCount; language++) {
        DrawBand(Entry(language), language == Cursor ? kLit : kDim);
    }
    if (Proc == LANGSET_SELECT) {
        g_hand_shown = ui::MenuPointerHand(g_hand_x, g_hand_y);
    }
    if (g_hand != nullptr && g_hand_shown) {
        // Drawn as the title draws it, without the menus' shadow.
        TexManager.ReloadTexture(GetVif1Packet(), 1);
        setbilinear(0);
        ui::Picture::Draw(g_hand, {static_cast<int>(g_hand_x), static_cast<int>(g_hand_y), kHand.w, kHand.h}, kHand, Fade.Get(kLit));
        setbilinear(1);
    }
}

} // namespace dc::game
