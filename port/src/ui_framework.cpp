#include "ui_framework.hpp"

#include <algorithm>
#include <cmath>

#include "clsmes.hpp"
#include "draw2d_port.hpp"
#include "gameutil.hpp"
#include "memcard.hpp"
#include "menu_draw.hpp"
#include "menuetc.hpp"
#include "mglib.hpp"
#include "texture.hpp"

extern char AtoraVibeTextureName[];
extern void MenuHelpWinDraw(int x, int y, float w, float h, int alpha);
extern void setbilinear(int n);

namespace UI {

// -----------------------------------------------------------------------------
// Word Wrap Utility
// -----------------------------------------------------------------------------
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
// Window
// -----------------------------------------------------------------------------
void Window::Draw(int x, int y, int width_px, int height_px, int alpha) {
    float w = (width_px - 28) / 16.0f;
    float h = (height_px - 76) / 16.0f;
    if (w < 0.0f) w = 0.0f;
    if (h < 0.0f) h = 0.0f;
    MenuHelpWinDraw(x, y, w, h, alpha);
}

// -----------------------------------------------------------------------------
// Label
// -----------------------------------------------------------------------------
Label::Label(std::string_view text, u32 color, Alignment align)
    : m_raw_text(text), m_color(color), m_align(align) {
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

int Label::GetWidth() {
    if (m_dirty) {
        m_text.RefreshGlyphs();
        m_cached_width = m_text.Width();
        m_dirty = false;
    }
    return m_cached_width;
}

void Label::Draw(int x, int y, int alpha) {
    int draw_x = x;
    int width = GetWidth();

    if (m_align == Alignment::Center) {
        draw_x -= (width / 2);
    } else if (m_align == Alignment::Right) {
        draw_x -= width;
    }

    m_text.SetColour(m_color);
    m_text.Draw(draw_x, y, alpha);
}

void Label::DrawWithStroke(int x, int y, u32 fill_color, u32 stroke_color, int stroke_px, int alpha) {
    int draw_x = x;
    int width = GetWidth();

    if (m_align == Alignment::Center) {
        draw_x -= (width / 2);
    } else if (m_align == Alignment::Right) {
        draw_x -= width;
    }

    m_text.SetColour(stroke_color);
    for (int dy = -stroke_px; dy <= stroke_px; ++dy) {
        for (int dx = -stroke_px; dx <= stroke_px; ++dx) {
            if (dx == 0 && dy == 0) continue;
            m_text.Draw(draw_x + dx, y + dy, alpha);
        }
    }

    m_text.SetColour(fill_color);
    m_text.Draw(draw_x, y, alpha);
    m_text.Draw(draw_x + 1, y, alpha);
}

void Label::DrawWrapped(int x, int y, int max_width, int line_height, int alpha) {
    std::vector<std::string> lines = WrapText(m_raw_text, max_width);
    for (size_t i = 0; i < lines.size(); ++i) {
        Label line_lbl(lines[i], m_color, m_align);
        line_lbl.Draw(x, y + static_cast<int>(i) * line_height, alpha);
    }
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

void Cursor::Draw(int alpha) {
    if (m_texture == nullptr) {
        m_texture = draw2d::Get().find_texture("gaiji");
        if (m_texture == nullptr) {
            m_texture = TexManager.GetTexture((char*)"gaiji", -1);
            if (m_texture == nullptr) return;
        }
    }

    m_cur_x += (m_target_x - m_cur_x) / 4.0f;
    m_cur_y += (m_target_y - m_cur_y) / 4.0f;

    float hand_x = m_cur_x + 7.0f * std::cos(0.0805536583f * static_cast<float>(m_anim_count));
    float hand_y = m_cur_y + 5.0f * std::sin(0.1163552850f * static_cast<float>(m_anim_count));
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
// Brackets
// -----------------------------------------------------------------------------
Brackets::Brackets() {
    m_texture = TexManager.GetTexture((char*)"option2", -1);
}

void Brackets::Draw(int left, int right, int top, int bottom, int alpha) {
    if (m_texture == nullptr) {
        m_texture = TexManager.GetTexture((char*)"option2", -1);
        if (m_texture == nullptr) return;
    }

    constexpr int kBracketBeats = 30;
    constexpr float kBracketClose = 4.0f;

    float pulse = kBracketClose * static_cast<float>(m_beat) / static_cast<float>(kBracketBeats - 1);
    m_beat = (m_beat + 1) % kBracketBeats;

    int x0 = static_cast<int>(left + pulse);
    int x1 = static_cast<int>(right - pulse);
    int y0 = static_cast<int>(top + pulse);
    int y1 = static_cast<int>(bottom - pulse);

    DrawMenu2DSprite(m_texture, CRect_i_(x0, y0, 16, 16), CRect_i_(0xB2, 0xF8, 16, 16), alpha);
    DrawMenu2DSprite(m_texture, CRect_i_(x1, y0, 16, 16), CRect_i_(0xC2, 0xF8, 16, 16), alpha);
    DrawMenu2DSprite(m_texture, CRect_i_(x0, y1, 16, 16), CRect_i_(0xB2, 0x108, 16, 16), alpha);
    DrawMenu2DSprite(m_texture, CRect_i_(x1, y1, 16, 16), CRect_i_(0xC2, 0x108, 16, 16), alpha);
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
    if (total_rows <= visible_rows || m_bar_frames <= 0) return;

    constexpr int kBarFade = 15;
    int cur_alpha = alpha * std::min(m_bar_frames, kBarFade) / kBarFade;

    if (m_track_tex == nullptr) m_track_tex = TexManager.GetTexture(AtoraVibeTextureName, -1);
    if (m_thumb_tex == nullptr) m_thumb_tex = TexManager.GetTexture((char*)"option2", -1);

    int track_top = top_y + 14;
    int track_height = height - 28;
    int thumb_height = std::max(12, track_height * visible_rows / total_rows);
    int thumb_y = track_top + (track_height - thumb_height) * first_row / (total_rows - visible_rows);

    if (m_track_tex) {
        DrawMenu2DSprite(m_track_tex, CRect_i_(x, track_top, 8, track_height), CRect_i_(22, 22, 16, 20), cur_alpha);
    }
    if (m_thumb_tex) {
        DrawMenu2DSprite(m_thumb_tex, CRect_i_(x, thumb_y, 8, thumb_height), CRect_i_(140, 280, 24, 32), cur_alpha);
        DrawMenu2DSprite(m_thumb_tex, CRect_i_(x - 4, top_y, 16, 16), CRect_i_(452, 136, 24, 24), cur_alpha);
        DrawMenu2DSprite(m_thumb_tex, CRect_i_(x - 4, top_y + height - 16, 16, 16), CRect_i_(452, 164, 24, 24), cur_alpha);
    }
}

} // namespace UI