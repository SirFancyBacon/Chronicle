#include "ui_framework/internal.hpp"

#include <algorithm>
#include <cmath>

#include "itemdata.hpp"
#include "menu_pointer.hpp"

namespace UI {

void ItemIcon::DrawSocket(int x, int y, int size, bool filled, int alpha) {
    CTexture* wep_tex = detail::GetWepStatusTex();
    if (wep_tex == nullptr || size <= 0) return;

    setbilinear(0);
    int      src_y = filled ? 0x10A : 0xE0;
    CRect_i_ src(0xD4, src_y, 0x2A, 0x2A);
    DrawMenu2DSprite(wep_tex, CRect_i_(x + 1, y + 2, size, size), src, 0, 0, 0, (alpha * 80) >> 7);
    DrawMenu2DSprite(wep_tex, CRect_i_(x, y, size, size), src, 0x80, 0x80, 0x80, alpha);
}

bool ItemIcon::Draw(int item_no, int x, int y, int size, int alpha, bool drop_shadow, bool with_socket) {
    if (item_no <= 0 || size <= 0) return false;

    if (with_socket) {
        DrawSocket(x - 2, y - 2, size + 4, false, alpha);
    }

    COM_ITEM_INFO* info = GetCommonItemInfo(item_no);
    if (info == nullptr || info->icon_index < 0) return false;

    if (::ItemIcon == nullptr) ::ItemIcon = detail::FindMenuTexture("itemicon");
    if (::WepIcon == nullptr)  ::WepIcon  = detail::FindMenuTexture("wepicon");

    int       u = 0, v = 0;
    CTexture* texture = RetCTex(static_cast<short>(item_no), u, v);
    if (texture == nullptr) return false;

    if (texture->block >= 0) {
        MenuTextureReload(texture->block);
    }

    setbilinear(0);
    CRect_i_ source(u + 1, v + 1, 30, 30);

    if (drop_shadow) {
        DrawMenu2DSprite(texture, CRect_i_(x + 2, y + 1, size, size), source, 0, 0, 0, (alpha * 0x50) >> 7);
    }
    DrawMenu2DSprite(texture, CRect_i_(x, y, size, size), source, alpha);

    return true;
}

Cursor::Cursor() {
    m_texture = detail::GetWhitePixelTex();
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
    m_pointing  = true;
    m_pointer_x = x;
    m_pointer_y = y;
}

void Cursor::ClearPointer() {
    m_pointing = false;
}

void Cursor::Draw(int alpha) {
    m_texture = detail::GetWhitePixelTex();
    if (m_texture == nullptr) return;

    float hand_x = 0.0f;
    float hand_y = 0.0f;
    float ptr_x  = 0.0f;
    float ptr_y  = 0.0f;

    if (m_pointing) {
        hand_x = m_pointer_x - 26.0f;
        hand_y = m_pointer_y - 14.0f;
    } else if (MenuPointerHand(ptr_x, ptr_y)) {
        hand_x = m_cur_x = ptr_x;
        hand_y = m_cur_y = ptr_y;
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
    CRect_i_ screen_rect(static_cast<int>(hand_x), static_cast<int>(hand_y), 0x20, 0x20);

    DrawMenu2DSprite(m_texture, shadow_rect, uv, 0, 0, 0, (alpha * 100) >> 7);
    DrawMenu2DSprite(m_texture, screen_rect, uv, alpha);
}

void Cursor::DeferDraw(int alpha) {
    LayerQueue::Submit(ZLayer::Cursor, [this, alpha]() {
        this->Draw(alpha);
    });
}

Brackets::Brackets() {
    m_texture = TexManager.GetTexture((char*)"option2", -1);
}

void Brackets::Draw(int left, int right, int top, int bottom, int alpha) {
    setbilinear(0);
    m_texture = TexManager.GetTexture((char*)"option2", -1);
    if (m_texture != nullptr) {
        constexpr int   kBracketBeats = 30;
        constexpr float kBracketClose = 4.0f;

        float pulse = kBracketClose * static_cast<float>(m_beat) / static_cast<float>(kBracketBeats - 1);
        m_beat      = (m_beat + 1) % kBracketBeats;

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
        return;
    }

    StayTex = detail::FindMenuTexture("stayframe", StayTex);
    if (StayTex != nullptr) {
        int box_w = std::max(16, right - left);
        int box_h = std::max(16, bottom - top);
        DrawMenuWaku(left - 14, top - 6, box_w + 12, box_h - 4, 0, StayTex, alpha);
    }
}

void Brackets::DeferDraw(const Rect& box, int alpha) {
    LayerQueue::Submit(ZLayer::Brackets, [this, box, alpha]() {
        this->Draw(box, alpha);
    });
}

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
        cur_alpha              = alpha * std::min(m_bar_frames, kBarFade) / kBarFade;
    }

    m_track_tex = TexManager.GetTexture(AtoraVibeTextureName, -1);
    m_thumb_tex = TexManager.GetTexture((char*)"option2", -1);

    int track_top    = top_y + 14;
    int track_height = height - 32;
    int thumb_height = std::max(12, track_height * visible_rows / total_rows);
    int thumb_y      = track_top + (track_height - thumb_height) * first_row / (total_rows - visible_rows);

    if (m_track_tex != nullptr) {
        DrawMenu2DSprite(m_track_tex, CRect_i_(x, track_top, 8, track_height), CRect_i_(22, 22, 16, 20), cur_alpha);
    } else if (CTexture* gaiji_tex = detail::GetWhitePixelTex()) {
        DrawMenu2DSprite(gaiji_tex, CRect_i_(x, track_top, 8, track_height), CRect_i_(0x6A, 0x6E, 1, 1), 25, 28, 30, cur_alpha);
    }

    if (m_thumb_tex != nullptr) {
        DrawMenu2DSprite(m_thumb_tex, CRect_i_(x, thumb_y, 8, thumb_height), CRect_i_(140, 280, 24, 32), cur_alpha);
        DrawMenu2DSprite(m_thumb_tex, CRect_i_(x - 4, top_y - 4, 16, 16), CRect_i_(452, 136, 24, 24), cur_alpha);
        DrawMenu2DSprite(m_thumb_tex, CRect_i_(x - 4, track_top + track_height + 2, 16, 16), CRect_i_(452, 164, 24, 24), cur_alpha);
    }
}

void ProgressBar::Draw(const Rect& box, float progress, std::string_view text,
                       const ProgressBarStyle& style, int alpha) {
    const float clamped  = std::clamp(progress, 0.0f, 1.0f);
    const bool  complete = (clamped >= 1.0f);

    TrackerWindow::DrawIndentation(box, true, alpha);

    const int max_fill_w = std::max(0, box.w - 6);
    const int fill_w     = static_cast<int>(std::round(clamped * static_cast<float>(max_fill_w)));

    if (fill_w > 0 && box.h > 6) {
        if (GRADATION_COLOR_INFO2* grad_src = GetGradationColorInfo2(complete ? 4 : 3)) {
            GRADATION_COLOR_INFO2 grad = *grad_src;
            for (int c = 0; c < 4; ++c) {
                grad.colors[c].r = static_cast<u8>((grad.colors[c].r * 3) / 4);
                grad.colors[c].g = static_cast<u8>((grad.colors[c].g * 3) / 4);
                grad.colors[c].b = static_cast<u8>((grad.colors[c].b * 3) / 4);
                grad.colors[c].a = static_cast<u8>((alpha * 96) >> 7);
            }
            CRect_i_ fill_rect(box.x + 3, box.y + 3, fill_w, box.h - 6);
            DrawMenuColorGradation(fill_rect, &grad.colors[0], &grad.colors[1], &grad.colors[2], &grad.colors[3]);
        }

        if (CTexture* tex = detail::GetWhitePixelTex()) {
            CRect_i_ uv_white(0x6A, 0x6E, 1, 1);
            DrawMenu2DSprite(tex, CRect_i_(box.x + 3, box.y + 3, fill_w, 1), uv_white, 0, 0, 0, (alpha * 55) >> 7);
            DrawMenu2DSprite(tex, CRect_i_(box.x + 3, box.y + 4, fill_w, 1), uv_white, 0, 0, 0, (alpha * 25) >> 7);
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
        Label lbl(text, txt_style);
        lbl.Draw(box.CenterX(), box.y - 2, alpha);
    }
}

} // namespace UI
