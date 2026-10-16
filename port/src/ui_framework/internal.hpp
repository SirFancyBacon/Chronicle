#pragma once

#include "battlemenu.hpp"
#include "draw2d_port.hpp"
#include "memcard.hpp"
#include "menu_draw.hpp"
#include "menu_inventory.hpp"
#include "menuetc.hpp"
#include "mglib.hpp"
#include "platform/config.hpp"
#include "texture.hpp"
#include "ui_framework.hpp"

extern char      AtoraVibeTextureName[];
extern CTexture* ItemIcon;
extern CTexture* WepIcon;
extern CTexture* StayTex;
extern CTexture* PerBoardTex;
extern CTexture* WepStatus;

extern void MenuHelpWinDraw(int x, int y, float w, float h, int alpha);
extern void setbilinear(int n);

namespace UI::detail {

inline constexpr float kBaseCapHeight = 14.5f;

struct ScopedShadow {
    int  saved_shadow = 0;
    bool active       = false;

    explicit ScopedShadow(bool enable_shadow) {
        if (!enable_shadow) {
            Config& cfg     = const_cast<Config&>(ConfigGet());
            saved_shadow    = cfg.text_shadow;
            cfg.text_shadow = 0;
            active          = true;
        }
    }

    ~ScopedShadow() {
        if (active) {
            Config& cfg     = const_cast<Config&>(ConfigGet());
            cfg.text_shadow = saved_shadow;
        }
    }
};

inline CTexture* FindMenuTexture(const char* name, CTexture* cached = nullptr) {
    if (cached != nullptr) {
        if (cached->block >= 0) MenuTextureReload(cached->block);
        return cached;
    }
    CTexture* tex = draw2d::Get().find_texture(name);
    if (tex == nullptr) {
        tex = TexManager.GetTexture(const_cast<char*>(name), -1);
    }
    if (tex != nullptr && tex->block >= 0) {
        MenuTextureReload(tex->block);
    }
    return tex;
}

inline CTexture* GetWhitePixelTex() { return FindMenuTexture("gaiji"); }
inline CTexture* GetPerBoardTex()   { return FindMenuTexture("perbrd", PerBoardTex); }
inline CTexture* GetWepStatusTex()  { return FindMenuTexture("wepstatus", WepStatus); }

inline void DrawRoundedRect(CTexture* tex, int x, int y, int w, int h,
                            unsigned char r, unsigned char g, unsigned char b, int alpha, bool rounded) {
    if (tex == nullptr || w <= 0 || h <= 0 || alpha <= 0) return;
    CRect_i_ uv_white(0x6A, 0x6E, 1, 1);

    if (!rounded || w < 6 || h < 6) {
        DrawMenu2DSprite(tex, CRect_i_(x, y, w, h), uv_white, r, g, b, alpha);
        return;
    }

    DrawMenu2DSprite(tex, CRect_i_(x + 2, y,         w - 4, 1),     uv_white, r, g, b, alpha);
    DrawMenu2DSprite(tex, CRect_i_(x + 1, y + 1,     w - 2, 1),     uv_white, r, g, b, alpha);
    DrawMenu2DSprite(tex, CRect_i_(x,     y + 2,     w,     h - 4), uv_white, r, g, b, alpha);
    DrawMenu2DSprite(tex, CRect_i_(x + 1, y + h - 2, w - 2, 1),     uv_white, r, g, b, alpha);
    DrawMenu2DSprite(tex, CRect_i_(x + 2, y + h - 1, w - 4, 1),     uv_white, r, g, b, alpha);
}

inline void ColorToRgb(u32 color, unsigned char (&out)[3]) {
    switch (color) {
        case ColorGold:
            out[0] = 235; out[1] = 192; out[2] = 75;
            break;
        case ColorYellow:
            out[0] = 240; out[1] = 232; out[2] = 120;
            break;
        case ColorBlue:
            out[0] = 110; out[1] = 175; out[2] = 255;
            break;
        case ColorWhite:
        default:
            out[0] = 240; out[1] = 238; out[2] = 232;
            break;
    }
}

} // namespace UI::detail