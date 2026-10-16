#pragma once

#include <string_view>

#include "texture.hpp"
#include "ui_framework/text.hpp"
#include "ui_framework/types.hpp"

namespace UI {

enum class ButtonPreset {
    Terracotta,
    AmberGold,
    StoneGreen
};

struct ButtonStyle {
    ButtonPreset preset        = ButtonPreset::Terracotta;
    u32          text_color    = ColorWhite;
    bool         drop_shadow   = false;
    bool         recessed_text = true;
    Alignment    align         = Alignment::Center;
    float        text_scale    = 1.01f;             // Reduced by ~1pt (from 1.08f)
    float        text_xscale   = 1.20f;
    FontWeight   text_weight   = FontWeight::Normal;
    bool         rounded       = true;
    int          brightness    = 0x80;
    int          text_dx       = 0;
    int          text_dy       = 0;

    static constexpr ButtonStyle Thick(ButtonPreset p = ButtonPreset::Terracotta, int bright = 0x80) {
        return ButtonStyle{
            .preset        = p,
            .text_color    = ColorWhite,
            .drop_shadow   = false,
            .recessed_text = true,
            .align         = Alignment::Center,
            .text_scale    = 1.01f,                 // Reduced by ~1pt (from 1.08f)
            .text_xscale   = 1.20f,
            .text_weight   = FontWeight::Normal,
            .rounded       = true,
            .brightness    = bright
        };
    }

    static constexpr ButtonStyle Thin(ButtonPreset p = ButtonPreset::Terracotta, int bright = 0x80) {
        return ButtonStyle{
            .preset        = p,
            .text_color    = ColorWhite,
            .drop_shadow   = false,
            .recessed_text = true,
            .align         = Alignment::Center,
            .text_scale    = 1.06f,                 // Reduced by ~1pt (from 1.14f)
            .text_xscale   = 1.02f,
            .text_weight   = FontWeight::Thin,
            .rounded       = true,
            .brightness    = bright
        };
    }
};

class BevelButton {
public:
    static void Draw(const Rect& box, std::string_view text,
                     const ButtonStyle& style = {}, int alpha = 0x80);

    static void Draw(int x, int y, int width, int height, std::string_view text,
                     u32 text_color = ColorWhite, int alpha = 0x80,
                     ButtonPreset preset = ButtonPreset::Terracotta,
                     Alignment align = Alignment::Center,
                     bool drop_shadow = false,
                     FontWeight weight = FontWeight::Normal);
};

class TextureButton {
public:
    static void Draw(const Rect& box, CTexture* texture, const Rect& uv,
                     int alpha = 0x80, std::string_view text = {},
                     const TextStyle& text_style = {});

    static void Draw(int x, int y, int width, int height,
                     CTexture* texture, int u, int v, int uv_w, int uv_h,
                     int alpha = 0x80, std::string_view text = {},
                     const TextStyle& text_style = {});
};

} // namespace UI