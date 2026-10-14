#include "ui_framework.hpp"

#include <algorithm>
#include <cmath>

#include "caption_text.hpp"
#include "captionfont.hpp"
#include "clsmes.hpp"
#include "draw2d_port.hpp"
#include "gameutil.hpp"
#include "gfx/gfx.hpp"
#include "itemdata.hpp"
#include "memcard.hpp"
#include "menu_draw.hpp"
#include "menu_inventory.hpp"
#include "menuetc.hpp"
#include "mglib.hpp"
#include "platform/config.hpp"
#include "texture.hpp"
#include "textwipe.hpp"

extern char AtoraVibeTextureName[];
extern void MenuHelpWinDraw(int x, int y, float w, float h, int alpha);
extern void setbilinear(int n);

namespace UI {

namespace {

constexpr float kBaseCapHeight = 14.5f;

struct ScopedShadow {
    int saved_shadow = 0;
    bool active = false;

    explicit ScopedShadow(bool enable_shadow) {
        if (!enable_shadow) {
            Config& cfg = const_cast<Config&>(ConfigGet());
            saved_shadow = cfg.text_shadow;
            cfg.text_shadow = 0;
            active = true;
        }
    }

    ~ScopedShadow() {
        if (active) {
            Config& cfg = const_cast<Config&>(ConfigGet());
            cfg.text_shadow = saved_shadow;
        }
    }
};

void ColorToRgb(u32 color, unsigned char (&out)[3]) {
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
            out[0] = 225; out[1] = 225; out[2] = 225;
            break;
    }
}

std::u32string DecodeUtf8(std::string_view in) {
    std::u32string out;
    for (size_t i = 0; i < in.size();) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        const int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1
                                       : (c >> 4) == 14  ? 2
                                       : (c >> 3) == 30  ? 3 : -1;
        if (extra < 0 || i + static_cast<size_t>(extra) >= in.size()) {
            out.push_back(U'?');
            i++;
            continue;
        }
        char32_t ch = extra == 0 ? c : (c & (0x3F >> extra));
        for (int k = 1; k <= extra; k++) {
            ch = (ch << 6) | (static_cast<unsigned char>(in[i + k]) & 0x3F);
        }
        out.push_back(ch);
        i += static_cast<size_t>(extra) + 1;
    }
    return out;
}

bool UseScaledRenderer(std::string_view text, float scale, FontWeight weight) {
    if (std::abs(scale - 1.0f) <= 0.001f && weight == FontWeight::Normal) {
        return false;
    }
    if (text.find('{') != std::string::npos) {
        return false;
    }
    return caption::Ready();
}

int MeasureScaledText(std::string_view text, float scale, FontWeight weight) {
    if (text.empty()) return 0;

    struct VariantGuard {
        explicit VariantGuard(bool thin) { capfont::UseVariant(thin); }
        ~VariantGuard() { capfont::UseVariant(false); }
    } guard(weight == FontWeight::Thin && caption::ThinReady());

    const gfx::LogicalMapping mapping = gfx::GetUiMapping(gfx::CurrentRenderTarget());
    const capfont::Metrics metrics = capfont::GetMetrics();
    const float cap_height = kBaseCapHeight * scale;
    const float em = cap_height / std::max(0.2f, metrics.cap);
    const int em_px = std::clamp(static_cast<int>(em * mapping.scale_x + 0.5f), 4, 512);
    const float gap = cap_height * 0.075f;

    float natural = 0.0f;
    int drawn = 0;
    for (char32_t ch : DecodeUtf8(text)) {
        capfont::Glyph glyph;
        if (ch == U' ') {
            natural += cap_height * 0.32f;
            drawn++;
        } else if (capfont::GetGlyph(ch, em_px, glyph) && glyph.width > 0) {
            natural += glyph.width / mapping.scale_x + gap;
            drawn++;
        }
    }
    if (drawn > 0) {
        natural -= gap;
    }
    return static_cast<int>(std::round(natural));
}

void DrawScaledText(std::string_view text, float center_x, float cap_top, float scale,
                    u32 color, FontWeight weight, bool drop_shadow, int alpha) {
    if (text.empty() || alpha <= 0) return;

    const float cap_height = kBaseCapHeight * scale;
    const float rows = draw2d::RowScale();
    const float fade = std::clamp(static_cast<float>(alpha) / 128.0f, 0.0f, 1.0f);

    caption::LineStyle style{};
    style.thin = (weight == FontWeight::Thin && caption::ThinReady());
    style.weight = (weight == FontWeight::Bold) ? 1.1f : 0.0f;
    style.ink_fit = false;

    if (drop_shadow && ConfigGet().text_shadow > 0) {
        caption::LineStyle shadow_style = style;
        shadow_style.fill[0] = 0;
        shadow_style.fill[1] = 0;
        shadow_style.fill[2] = 0;
        shadow_style.edge[0] = 0;
        shadow_style.edge[1] = 0;
        shadow_style.edge[2] = 0;
        shadow_style.edge_width = 0.8f;
        shadow_style.opacity = std::clamp(static_cast<float>(ConfigGet().text_shadow) / 100.0f, 0.0f, 1.0f);

        const float s_dx = 2.0f * scale;
        const float s_dy = 2.0f * scale;
        caption::DrawLine(text, center_x + s_dx, cap_top + s_dy, cap_height, 1.0f, 0.0f, shadow_style, fade, rows);
    }

    ColorToRgb(color, style.fill);
    style.opacity = 1.0f;
    caption::DrawLine(text, center_x, cap_top, cap_height, 1.0f, 0.0f, style, fade, rows);
}

} // namespace

std::vector<std::string> WrapText(std::string_view text, int max_width) {
    std::vector<std::string> lines;
    if (text.empty() || max_width <= 0) return lines;

    std::string current_line;
    size_t i = 0;

    while (i < text.size()) {
        if (text[i] == '\r') {
            i++;
            continue;
        }
        if (text[i] == '\n') {
            lines.push_back(current_line);
            current_line.clear();
            i++;
            continue;
        }

        if (current_line.empty()) {
            while (i < text.size() && text[i] == ' ') i++;
            if (i >= text.size()) break;
        }

        size_t word_start = i;
        while (i < text.size() && text[i] != ' ' && text[i] != '\n' && text[i] != '\r') {
            i++;
        }
        std::string word(text.substr(word_start, i - word_start));

        if (i < text.size() && text[i] == ' ') {
            i++;
        }

        std::string candidate = current_line.empty() ? word : (current_line + " " + word);

        Label test_lbl(candidate);
        if (test_lbl.GetWidth() > max_width && !current_line.empty()) {
            lines.push_back(current_line);
            current_line = word;
        } else {
            current_line = candidate;
        }
    }

    if (!current_line.empty()) {
        lines.push_back(current_line);
    }

    return lines;
}

// -----------------------------------------------------------------------------
// Windows & Plates
// -----------------------------------------------------------------------------
void TrackerWindow::Draw(int x, int y, int width_px, int height_px, int alpha) {
    float w = (width_px - 28) / 16.0f;
    float h = (height_px - 76) / 16.0f;
    if (w < 0.0f) w = 0.0f;
    if (h < 0.0f) h = 0.0f;
    MenuHelpWinDraw(x, y, w, h, alpha);
}

void HelpBox::Draw(int x, int y, int width_px, int height_px, int alpha) {
    float w = (width_px - 48) / 16.0f;
    float h = (height_px - 44) / 16.0f;
    if (w < 0.0f) w = 0.0f;
    if (h < 0.0f) h = 0.0f;
    MenuHelpWinDraw(x, y, w, h, alpha);
}

void Window::Draw(int x, int y, int width_px, int height_px, int alpha) {
    TrackerWindow::Draw(x, y, width_px, height_px, alpha);
}

void TabPlate::Draw(int left, int right, int y, int alpha) {
    constexpr int kPlateSide = 24;
    constexpr int kPlateHeight = 44;
    float middle = static_cast<float>(right - left + 28 - 2 * kPlateSide) / 16.0f;
    if (middle < 0.0f) middle = 0.0f;
    MenuHelpWinDraw(left - 14, y - (kPlateHeight - 20) / 2, middle, 0.0f, alpha);
}

// -----------------------------------------------------------------------------
// Label
// -----------------------------------------------------------------------------
Label::Label(std::string_view text, u32 color, Alignment align, bool drop_shadow,
             float scale, FontWeight weight)
    : m_raw_text(text),
      m_color(color),
      m_align(align),
      m_drop_shadow(drop_shadow),
      m_scale(scale),
      m_weight(weight) {
    m_text.Set(m_raw_text.c_str());
}

Label::Label(std::string_view text, const TextStyle& style)
    : m_raw_text(text),
      m_color(style.color),
      m_align(style.align),
      m_drop_shadow(style.drop_shadow),
      m_scale(style.scale),
      m_weight(style.weight),
      m_dx(style.text_dx),
      m_dy(style.text_dy) {
    m_text.Set(m_raw_text.c_str());
}

void Label::SetText(std::string_view text) {
    m_raw_text = text;
    m_text.Set(m_raw_text.c_str());
    m_dirty = true;
}

void Label::SetColor(u32 color) {
    m_color = color;
}

void Label::SetAlignment(Alignment align) {
    m_align = align;
}

void Label::SetDropShadow(bool enabled) {
    m_drop_shadow = enabled;
}

void Label::SetScale(float scale) {
    m_scale = scale;
    m_dirty = true;
}

void Label::SetWeight(FontWeight weight) {
    m_weight = weight;
    m_dirty = true;
}

void Label::SetOffset(int dx, int dy) {
    m_dx = dx;
    m_dy = dy;
}

void Label::SetStyle(const TextStyle& style) {
    m_color = style.color;
    m_align = style.align;
    m_drop_shadow = style.drop_shadow;
    m_scale = style.scale;
    m_weight = style.weight;
    m_dx = style.text_dx;
    m_dy = style.text_dy;
    m_dirty = true;
}

int Label::GetWidth() {
    if (UseScaledRenderer(m_raw_text, m_scale, m_weight)) {
        m_cached_width = MeasureScaledText(m_raw_text, m_scale, m_weight);
        m_dirty = false;
        return m_cached_width;
    }
    m_text.RefreshGlyphs();
    m_cached_width = m_text.Width();
    m_dirty = false;
    return m_cached_width;
}

void Label::Draw(int x, int y, int alpha) {
    int draw_x = x + m_dx;
    int draw_y = y + m_dy;
    int width = GetWidth();

    if (UseScaledRenderer(m_raw_text, m_scale, m_weight)) {
        float center_x = static_cast<float>(draw_x);
        if (m_align == Alignment::Left) {
            center_x = static_cast<float>(draw_x) + static_cast<float>(width) * 0.5f;
        } else if (m_align == Alignment::Right) {
            center_x = static_cast<float>(draw_x) - static_cast<float>(width) * 0.5f;
        }
        float cap_height = kBaseCapHeight * m_scale;
        float cap_top = static_cast<float>(draw_y) + 3.0f + (kBaseCapHeight - cap_height) * 0.5f;
        DrawScaledText(m_raw_text, center_x, cap_top, m_scale, m_color, m_weight, m_drop_shadow, alpha);
        return;
    }

    if (m_align == Alignment::Center) {
        draw_x -= (width / 2);
    } else if (m_align == Alignment::Right) {
        draw_x -= width;
    }

    ScopedShadow shadow_guard(m_drop_shadow);
    m_text.SetColour(m_color);
    m_text.Draw(draw_x, draw_y, alpha);
}

void Label::DrawInRect(const Rect& box, int alpha) {
    if (UseScaledRenderer(m_raw_text, m_scale, m_weight)) {
        int width = GetWidth();
        float center_x = static_cast<float>(box.x) + static_cast<float>(box.w) * 0.5f;
        if (m_align == Alignment::Left) {
            center_x = static_cast<float>(box.x + 6) + static_cast<float>(width) * 0.5f;
        } else if (m_align == Alignment::Right) {
            center_x = static_cast<float>(box.x + box.w - 6) - static_cast<float>(width) * 0.5f;
        }
        float cap_height = kBaseCapHeight * m_scale;
        float cap_top = static_cast<float>(box.y) + (static_cast<float>(box.h) - cap_height) * 0.5f - 1.0f;
        DrawScaledText(m_raw_text, center_x + static_cast<float>(m_dx), cap_top + static_cast<float>(m_dy),
                       m_scale, m_color, m_weight, m_drop_shadow, alpha);
        return;
    }

    const textwipe::MessageContext msg_ctx(textwipe::Kind::Message);

    int text_w = GetWidth();
    int draw_x = box.x;

    if (m_align == Alignment::Center) {
        draw_x = box.x + (box.w - text_w) / 2;
    } else if (m_align == Alignment::Right) {
        draw_x = box.x + box.w - text_w - 6;
    } else {
        draw_x = box.x + 6;
    }

    int draw_y = box.y + (box.h - 22) / 2;

    ScopedShadow shadow_guard(m_drop_shadow);
    m_text.SetColour(m_color);
    m_text.Draw(draw_x + m_dx, draw_y + m_dy, alpha);
}

void Label::DrawCenteredInRect(int x, int y, int width, int height, int alpha) {
    Alignment prev = m_align;
    m_align = Alignment::Center;
    DrawInRect(Rect(x, y, width, height), alpha);
    m_align = prev;
}

void Label::DrawWithStroke(int x, int y, u32 fill_color, u32 stroke_color, int stroke_px, int alpha) {
    int draw_x = x + m_dx;
    int draw_y = y + m_dy;
    int width = GetWidth();

    if (m_align == Alignment::Center) {
        draw_x -= (width / 2);
    } else if (m_align == Alignment::Right) {
        draw_x -= width;
    }

    ScopedShadow shadow_guard(m_drop_shadow);
    m_text.SetColour(stroke_color);
    for (int dy = -stroke_px; dy <= stroke_px; ++dy) {
        for (int dx = -stroke_px; dx <= stroke_px; ++dx) {
            if (dx == 0 && dy == 0) continue;
            m_text.Draw(draw_x + dx, draw_y + dy, alpha);
        }
    }

    m_text.SetColour(fill_color);
    m_text.Draw(draw_x, draw_y, alpha);
    m_text.Draw(draw_x + 1, draw_y, alpha);
}

void Label::DrawWrapped(int x, int y, int max_width, int line_height, int alpha) {
    std::vector<std::string> lines = WrapText(m_raw_text, max_width);
    for (size_t i = 0; i < lines.size(); ++i) {
        Label line_lbl(lines[i], m_color, m_align, m_drop_shadow, m_scale, m_weight);
        line_lbl.SetOffset(m_dx, m_dy);
        line_lbl.Draw(x, y + static_cast<int>(i) * line_height, alpha);
    }
}

void DrawTextBox(std::string_view text, const Rect& box, const TextStyle& style, int alpha) {
    Label lbl(text, style);
    lbl.DrawInRect(box, alpha);
}

// -----------------------------------------------------------------------------
// ItemIcon: Looks up item UV in ::ItemIcon / ::WepIcon and renders scaled icon
// -----------------------------------------------------------------------------
bool ItemIcon::Draw(int item_no, int x, int y, int size, int alpha, bool drop_shadow) {
    if (item_no <= 0 || size <= 0) return false;

    COM_ITEM_INFO* info = GetCommonItemInfo(item_no);
    if (info == nullptr || info->icon_index < 0) return false;

    if (::ItemIcon == nullptr) {
        ::ItemIcon = draw2d::Get().find_texture("itemicon");
        if (::ItemIcon == nullptr) {
            ::ItemIcon = TexManager.GetTexture((char*)"itemicon", -1);
        }
    }
    if (::WepIcon == nullptr) {
        ::WepIcon = draw2d::Get().find_texture("wepicon");
        if (::WepIcon == nullptr) {
            ::WepIcon = TexManager.GetTexture((char*)"wepicon", -1);
        }
    }

    int u = 0;
    int v = 0;
    CTexture* texture = RetCTex(static_cast<short>(item_no), u, v);
    if (texture == nullptr) {
        return false;
    }

    if (texture->block >= 0) {
        MenuTextureReload(texture->block);
    }

    // Disable bilinear filtering and inset UV by 1px on all sides to prevent neighboring atlas bleed
    setbilinear(0);
    CRect_i_ source(u + 1, v + 1, 30, 30);

    if (drop_shadow) {
        DrawMenu2DSprite(texture, CRect_i_(x + 2, y + 1, size, size), source, 0, 0, 0, (alpha * 0x50) >> 7);
    }
    DrawMenu2DSprite(texture, CRect_i_(x, y, size, size), source, alpha);
    setbilinear(1);

    return true;
}

// -----------------------------------------------------------------------------
// Cursor
// -----------------------------------------------------------------------------
Cursor::Cursor() {
    m_texture = draw2d::Get().find_texture("gaiji");
    if (m_texture == nullptr) {
        m_texture = TexManager.GetTexture((char*)"gaiji", -1);
    }
}

void Cursor::SetTarget(float x, float y) {
    m_target_x = x;
    m_target_y = y;
}

void Cursor::SnapTo(float x, float y) {
    m_cur_x = m_target_x = x;
    m_cur_y = m_target_y = y;
}

void Cursor::SetPointer(float x, float y) {
    m_pointing = true;
    m_pointer_x = x;
    m_pointer_y = y;
}

void Cursor::ClearPointer() {
    m_pointing = false;
}

void Cursor::Draw(int alpha) {
    m_texture = draw2d::Get().find_texture("gaiji");
    if (m_texture == nullptr) {
        m_texture = TexManager.GetTexture((char*)"gaiji", -1);
        if (m_texture == nullptr) return;
    }

    float hand_x = 0.0f;
    float hand_y = 0.0f;

    if (m_pointing) {
        hand_x = m_pointer_x - 26.0f;
        hand_y = m_pointer_y - 14.0f;
    } else {
        m_cur_x += (m_target_x - m_cur_x) / 4.0f;
        m_cur_y += (m_target_y - m_cur_y) / 4.0f;
        hand_x = m_cur_x + 7.0f * std::cos(0.0805536583f * static_cast<float>(m_anim_count));
        hand_y = m_cur_y + 5.0f * std::sin(0.1163552850f * static_cast<float>(m_anim_count));
    }
    m_anim_count = (m_anim_count + 1) % 0x107AC0;

    setbilinear(0);
    CRect_i_ uv(0x60, 0x60, 0x20, 0x20);

    CRect_i_ shadow_rect(static_cast<int>(hand_x + 5.0f), static_cast<int>(hand_y + 3.0f), 0x20, 0x20);
    DrawMenu2DSprite(m_texture, shadow_rect, uv, 0, 0, 0, (alpha * 100) >> 7);

    CRect_i_ screen_rect(static_cast<int>(hand_x), static_cast<int>(hand_y), 0x20, 0x20);
    DrawMenu2DSprite(m_texture, screen_rect, uv, alpha);

    setbilinear(1);
}

// -----------------------------------------------------------------------------
// Brackets: Scaled to single-line proportions with bounded corners
// -----------------------------------------------------------------------------
Brackets::Brackets() {
    m_texture = TexManager.GetTexture((char*)"option2", -1);
}

void Brackets::Draw(int left, int right, int top, int bottom, int alpha) {
    m_texture = TexManager.GetTexture((char*)"option2", -1);
    if (m_texture == nullptr) return;

    constexpr int kBracketBeats = 30;
    constexpr float kBracketClose = 4.0f;

    float pulse = kBracketClose * static_cast<float>(m_beat) / static_cast<float>(kBracketBeats - 1);
    m_beat = (m_beat + 1) % kBracketBeats;

    int x0 = static_cast<int>(left + pulse);
    int x1 = static_cast<int>(right - pulse);
    int y0 = static_cast<int>(top + pulse);
    int y1 = static_cast<int>(bottom - pulse);

    constexpr int corner_w = 12;
    constexpr int corner_h = 8;

    DrawMenu2DSprite(m_texture, CRect_i_(x0, y0, corner_w, corner_h), CRect_i_(0xB2, 0xF8, 16, 16), alpha);
    DrawMenu2DSprite(m_texture, CRect_i_(x1 - corner_w, y0, corner_w, corner_h), CRect_i_(0xC2, 0xF8, 16, 16), alpha);
    DrawMenu2DSprite(m_texture, CRect_i_(x0, y1 - corner_h, corner_w, corner_h), CRect_i_(0xB2, 0x108, 16, 16), alpha);
    DrawMenu2DSprite(m_texture, CRect_i_(x1 - corner_w, y1 - corner_h, corner_w, corner_h), CRect_i_(0xC2, 0x108, 16, 16), alpha);
}

// -----------------------------------------------------------------------------
// ScrollBar
// -----------------------------------------------------------------------------
ScrollBar::ScrollBar() {
    m_track_tex = TexManager.GetTexture(AtoraVibeTextureName, -1);
    m_thumb_tex = TexManager.GetTexture((char*)"option2", -1);
}

void ScrollBar::Update(bool moved) {
    constexpr int kBarFrames = 75;
    if (moved) {
        m_bar_frames = kBarFrames;
    } else if (m_bar_frames > 0) {
        m_bar_frames--;
    }
}

void ScrollBar::Draw(int x, int top_y, int height, int total_rows, int visible_rows, int first_row, int alpha) {
    if (total_rows <= visible_rows || alpha <= 0) return;

    int cur_alpha = alpha;
    if (m_bar_frames > 0) {
        constexpr int kBarFade = 15;
        cur_alpha = alpha * std::min(m_bar_frames, kBarFade) / kBarFade;
    }

    m_track_tex = TexManager.GetTexture(AtoraVibeTextureName, -1);
    m_thumb_tex = TexManager.GetTexture((char*)"option2", -1);

    int track_top = top_y + 14;
    int track_height = height - 32;
    int thumb_height = std::max(12, track_height * visible_rows / total_rows);
    int thumb_y = track_top + (track_height - thumb_height) * first_row / (total_rows - visible_rows);

    if (m_track_tex != nullptr) {
        DrawMenu2DSprite(m_track_tex, CRect_i_(x, track_top, 8, track_height), CRect_i_(22, 22, 16, 20), cur_alpha);
    } else {
        CTexture* gaiji_tex = draw2d::Get().find_texture("gaiji");
        if (gaiji_tex == nullptr) gaiji_tex = TexManager.GetTexture((char*)"gaiji", -1);
        if (gaiji_tex != nullptr) {
            DrawMenu2DSprite(gaiji_tex, CRect_i_(x, track_top, 8, track_height), CRect_i_(0x6A, 0x6E, 1, 1), 25, 28, 30, cur_alpha);
        }
    }

    if (m_thumb_tex != nullptr) {
        DrawMenu2DSprite(m_thumb_tex, CRect_i_(x, thumb_y, 8, thumb_height), CRect_i_(140, 280, 24, 32), cur_alpha);
        DrawMenu2DSprite(m_thumb_tex, CRect_i_(x - 4, top_y - 4, 16, 16), CRect_i_(452, 136, 24, 24), cur_alpha);
        DrawMenu2DSprite(m_thumb_tex, CRect_i_(x - 4, track_top + track_height + 2, 16, 16), CRect_i_(452, 164, 24, 24), cur_alpha);
    }
}

// -----------------------------------------------------------------------------
// BevelButton: Procedural beveled button with justified/centered text
// -----------------------------------------------------------------------------
void BevelButton::Draw(const Rect& box, std::string_view text,
                       const ButtonStyle& style, int alpha) {
    CTexture* gaiji_tex = draw2d::Get().find_texture("gaiji");
    if (gaiji_tex == nullptr) {
        gaiji_tex = TexManager.GetTexture((char*)"gaiji", -1);
    }

    if (gaiji_tex != nullptr) {
        CRect_i_ uv_white(0x6A, 0x6E, 1, 1);

        DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x + 2, box.y + 3, box.w, box.h), uv_white, 0, 0, 0, (alpha * 90) >> 7);

        if (style.preset == ButtonPreset::Terracotta) {
            DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x, box.y, box.w, box.h), uv_white, 20, 10, 5, alpha);
            DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x + 1, box.y + 1, box.w - 2, box.h - 2), uv_white, 115, 75, 50, alpha);
            DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x + 2, box.y + 2, box.w - 3, box.h - 3), uv_white, 55, 28, 14, alpha);
            DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x + 2, box.y + 2, box.w - 4, box.h - 4), uv_white, 100, 56, 33, alpha);
        } else { // StoneGreen
            DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x, box.y, box.w, box.h), uv_white, 15, 20, 15, alpha);
            DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x + 1, box.y + 1, box.w - 2, box.h - 2), uv_white, 80, 110, 85, alpha);
            DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x + 2, box.y + 2, box.w - 3, box.h - 3), uv_white, 30, 45, 35, alpha);
            DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x + 2, box.y + 2, box.w - 4, box.h - 4), uv_white, 45, 70, 50, alpha);
        }
    }

    if (!text.empty()) {
        MenuTextureReload(0x1A);
        TextStyle txt_style{
            .color       = style.text_color,
            .drop_shadow = style.drop_shadow,
            .align       = style.align,
            .scale       = style.text_scale,
            .weight      = style.text_weight,
            .text_dx     = style.text_dx,
            .text_dy     = style.text_dy
        };
        DrawTextBox(text, box, txt_style, alpha);
    }
}

void BevelButton::Draw(int x, int y, int width, int height, std::string_view text,
                       u32 text_color, int alpha, ButtonPreset preset,
                       Alignment align, bool drop_shadow) {
    ButtonStyle style{
        .preset      = preset,
        .text_color  = text_color,
        .drop_shadow = drop_shadow,
        .align       = align
    };
    Draw(Rect(x, y, width, height), text, style, alpha);
}

// -----------------------------------------------------------------------------
// TextureButton: Sprite-backed button with optional text overlay
// -----------------------------------------------------------------------------
void TextureButton::Draw(const Rect& box, CTexture* texture, const Rect& uv,
                         int alpha, std::string_view text,
                         const TextStyle& text_style) {
    if (texture != nullptr && !uv.Empty()) {
        DrawMenu2DSprite(texture, CRect_i_(box.x, box.y, box.w, box.h),
                         CRect_i_(uv.x, uv.y, uv.w, uv.h), alpha);
    }

    if (!text.empty()) {
        MenuTextureReload(0x1A);
        DrawTextBox(text, box, text_style, alpha);
    }
}

void TextureButton::Draw(int x, int y, int width, int height,
                         CTexture* texture, int u, int v, int uv_w, int uv_h,
                         int alpha, std::string_view text,
                         const TextStyle& text_style) {
    Draw(Rect(x, y, width, height), texture, Rect(u, v, uv_w, uv_h), alpha, text, text_style);
}

// -----------------------------------------------------------------------------
// ProgressBar: Recessed gauge bar with proportional fill and scaled text
// -----------------------------------------------------------------------------
void ProgressBar::Draw(const Rect& box, float progress, std::string_view text,
                       const ProgressBarStyle& style, int alpha) {
    CTexture* gaiji_tex = draw2d::Get().find_texture("gaiji");
    if (gaiji_tex == nullptr) {
        gaiji_tex = TexManager.GetTexture((char*)"gaiji", -1);
    }

    const float clamped = std::clamp(progress, 0.0f, 1.0f);
    const bool  complete = (clamped >= 1.0f);

    if (gaiji_tex != nullptr) {
        CRect_i_ uv_white(0x6A, 0x6E, 1, 1);

        // Drop shadow
        DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x + 2, box.y + 3, box.w, box.h), uv_white, 0, 0, 0, (alpha * 90) >> 7);
        // Outer dark perimeter
        DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x, box.y, box.w, box.h), uv_white, 14, 16, 20, alpha);
        // Recessed top/left shadow bevel
        DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x + 1, box.y + 1, box.w - 2, box.h - 2), uv_white, 10, 12, 15, alpha);
        // Recessed bottom/right highlight bevel
        DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x + 2, box.y + 2, box.w - 3, box.h - 3), uv_white, 48, 52, 58, alpha);
        // Inner dark trough background
        DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x + 2, box.y + 2, box.w - 4, box.h - 4), uv_white, 18, 20, 24, alpha);

        // Proportional gauge fill
        const int max_fill_w = std::max(0, box.w - 4);
        const int fill_w = static_cast<int>(std::round(clamped * static_cast<float>(max_fill_w)));

        if (fill_w > 0 && box.h > 4) {
            u8 r  = complete ? style.done_r    : style.fill_r;
            u8 g  = complete ? style.done_g    : style.fill_g;
            u8 b  = complete ? style.done_b    : style.fill_b;
            u8 hr = complete ? style.done_hi_r : style.hi_r;
            u8 hg = complete ? style.done_hi_g : style.hi_g;
            u8 hb = complete ? style.done_hi_b : style.hi_b;

            DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x + 2, box.y + 2, fill_w, box.h - 4), uv_white, r, g, b, alpha);
            if (box.h > 6) {
                DrawMenu2DSprite(gaiji_tex, CRect_i_(box.x + 2, box.y + 2, fill_w, 2), uv_white, hr, hg, hb, alpha);
            }
        }
    }

    if (!text.empty()) {
        MenuTextureReload(0x1A);
        TextStyle txt_style{
            .color       = complete ? ColorGold : style.text_color,
            .drop_shadow = style.drop_shadow,
            .align       = Alignment::Center,
            .scale       = style.text_scale,
            .weight      = style.text_weight
        };
        DrawTextBox(text, box, txt_style, alpha);
    }
}

} // namespace UI