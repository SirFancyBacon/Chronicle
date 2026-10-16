#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "clsmes.hpp"
#include "gametext.hpp"
#include "ui_framework/types.hpp"

namespace UI {

struct TextStyle {
    u32        color          = ColorWhite;
    bool       drop_shadow    = true;
    Alignment  align          = Alignment::Center;
    float      scale          = 1.0f;
    float      xscale         = 1.0f;
    float      yscale         = 1.0f;
    FontWeight weight         = FontWeight::Normal;
    int        line_height    = 24;
    int        text_dx        = 0;
    int        text_dy        = 0;
    bool       use_custom_rgb = false;
    u8         fill_r         = 240;
    u8         fill_g         = 238;
    u8         fill_b         = 232;
    float      outline_width  = 0.0f;
    u8         outline_r      = 16;
    u8         outline_g      = 28;
    u8         outline_b      = 68;
    bool       raised_bevel   = false;
    u8         bevel_r        = 108;
    u8         bevel_g        = 134;
    u8         bevel_b        = 192;
    bool       recessed       = false;
    u8         recess_dark_r  = 72;
    u8         recess_dark_g  = 34;
    u8         recess_dark_b  = 18;
    u8         recess_light_r = 250;
    u8         recess_light_g = 195;
    u8         recess_light_b = 155;
};

std::vector<std::string> WrapText(std::string_view text, int max_width, float scale = 1.0f);

class Label {
public:
    Label() = default;
    explicit Label(std::string_view text, u32 color = ColorWhite,
                   Alignment align = Alignment::Left, bool drop_shadow = true,
                   float scale = 1.0f, FontWeight weight = FontWeight::Normal,
                   float xscale = 1.0f);
    Label(std::string_view text, const TextStyle& style);

    void SetText(std::string_view text);
    void SetColor(u32 color);
    void SetCustomRgb(u8 r, u8 g, u8 b);
    void SetOutline(float width, u8 r = 16, u8 g = 28, u8 b = 68);
    void SetAlignment(Alignment align);
    void SetDropShadow(bool enabled);
    void SetScale(float scale);
    void SetXScale(float xscale);
    void SetYScale(float yscale);
    void SetWeight(FontWeight weight);
    void SetOffset(int dx, int dy);
    void SetStyle(const TextStyle& style);

    const std::string& GetText() const { return m_raw_text; }
    int GetWidth();

    void Draw(int x, int y, int alpha = 0x80);
    void DrawInRect(const Rect& box, int alpha = 0x80);
    void DrawCenteredInRect(int x, int y, int width, int height, int alpha = 0x80);
    void DrawAnchored(const Rect& parent, Anchor anchor, int offset_x = 0, int offset_y = 0, int alpha = 0x80);
    void DrawWithStroke(int x, int y, u32 fill_color, u32 stroke_color, int stroke_px = 2, int alpha = 0x80);
    void DrawWrapped(int x, int y, int max_width, int line_height = 24, int alpha = 0x80);

private:
    std::string m_raw_text;
    GameText    m_text;
    u32         m_color{ColorWhite};
    Alignment   m_align{Alignment::Left};
    bool        m_drop_shadow{true};
    float       m_scale{1.0f};
    float       m_xscale{1.0f};
    float       m_yscale{1.0f};
    FontWeight  m_weight{FontWeight::Normal};
    int         m_dx{0};
    int         m_dy{0};
    bool        m_use_custom_rgb{false};
    u8          m_fill_r{240};
    u8          m_fill_g{238};
    u8          m_fill_b{232};
    float       m_outline_width{0.0f};
    u8          m_outline_r{16};
    u8          m_outline_g{28};
    u8          m_outline_b{68};
    bool        m_raised_bevel{false};
    u8          m_bevel_r{108};
    u8          m_bevel_g{134};
    u8          m_bevel_b{192};
    bool        m_recessed{false};
    u8          m_recess_dark_r{72};
    u8          m_recess_dark_g{34};
    u8          m_recess_dark_b{18};
    u8          m_recess_light_r{250};
    u8          m_recess_light_g{195};
    u8          m_recess_light_b{155};
    bool        m_dirty{true};
    int         m_cached_width{0};
};

void DrawTextBox(std::string_view text, const Rect& box, const TextStyle& style = {}, int alpha = 0x80);

void DrawCounter(int x, int y, int current_val, int max_val,
                 float scale = Metrics::kScaleBody, int alpha = 0x80,
                 u32 normal_color = ColorWhite, u32 complete_color = ColorGold);

void DrawStatusText(std::string_view text, int x, int y, Alignment align,
                    u8 fill_r, u8 fill_g, u8 fill_b,
                    float scale = 0.78f, int alpha = 0x80,
                    u8 out_r = 14, u8 out_g = 26, u8 out_b = 64);

void DrawStatusNumber(int value, int right_x, int y,
                      float scale = 0.78f, int alpha = 0x80,
                      bool highlight_red = false);

void DrawStatusSlashPair(int current_val, int max_val, int slash_x, int y,
                         float scale = 0.76f, int alpha = 0x80);

} // namespace UI
