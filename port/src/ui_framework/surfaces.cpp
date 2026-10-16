#include "ui_framework/internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

#include "gfx/gfx.hpp"
#include "localize.hpp"
#include "platform/imagefile.hpp"
#include "texture_port.hpp"

namespace UI {

namespace {

struct SurfaceSpec {
    const char* plate_path;
    const char* tex_name;
    int         seed_offset;
    float       base_r, base_g, base_b;
    float       scale_r, scale_g, scale_b;
    float       bevel_r, bevel_g, bevel_b;
    float       rim_r, rim_g, rim_b;
    float       macro_weight;
    float       med_weight;
    float       detail_weight;
    float       ridge_weight;
    float       tooth_weight;
    float       bump_strength;
    float       dither_strength;
    float       bevel_strength;
};

constexpr SurfaceSpec kBlueStoneSpec{
    .plate_path      = "textures/plates/ui_stone.png",
    .tex_name        = "ui_stone_win",
    .seed_offset     = 0,
    .base_r          = 78.0f,  .base_g  = 92.0f,  .base_b  = 116.0f,
    .scale_r         = 0.88f,  .scale_g = 1.00f,  .scale_b = 1.14f,
    .bevel_r         = 1.00f,  .bevel_g = 1.05f,  .bevel_b = 1.12f,
    .rim_r           = 22.0f,  .rim_g   = 28.0f,  .rim_b   = 40.0f,
    .macro_weight    = 34.0f,
    .med_weight      = 22.0f,
    .detail_weight   = 12.0f,
    .ridge_weight    = 14.0f,
    .tooth_weight    = 0.085f,
    .bump_strength   = 32.0f,
    .dither_strength = 2.2f,
    .bevel_strength  = 40.0f
};

constexpr SurfaceSpec kPaperSpec{
    .plate_path      = "textures/plates/ui_paper.png",
    .tex_name        = "ui_paper_win",
    .seed_offset     = 1000,
    .base_r          = 160.0f, .base_g  = 128.0f, .base_b  = 90.0f,
    .scale_r         = 0.95f,  .scale_g = 0.88f,  .scale_b = 0.72f,
    .bevel_r         = 1.05f,  .bevel_g = 0.95f,  .bevel_b = 0.80f,
    .rim_r           = 58.0f,  .rim_g   = 44.0f,  .rim_b   = 28.0f,
    .macro_weight    = 30.0f,
    .med_weight      = 20.0f,
    .detail_weight   = 22.0f,
    .ridge_weight    = 12.0f,
    .tooth_weight    = 0.075f,
    .bump_strength   = 40.0f,
    .dither_strength = 4.0f,
    .bevel_strength  = 64.0f
};

const SurfaceSpec& GetSurfaceSpec(WindowPreset preset) {
    return (preset == WindowPreset::Paper) ? kPaperSpec : kBlueStoneSpec;
}

float Hash2D(int ix, int iy, int seed) {
    uint32_t n = static_cast<uint32_t>(ix) * 374761393u +
                 static_cast<uint32_t>(iy) * 668265263u +
                 static_cast<uint32_t>(seed) * 1442695041u;
    n = (n ^ (n >> 13)) * 1274126177u;
    n = n ^ (n >> 16);
    return static_cast<float>(n) / 4294967295.0f;
}

float SmoothNoisePow2(float x, float y, int px_mask, int py_mask, int seed) {
    int   x0 = static_cast<int>(std::floor(x));
    int   y0 = static_cast<int>(std::floor(y));
    float fx = x - static_cast<float>(x0);
    float fy = y - static_cast<float>(y0);
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);

    int ix0 = x0 & px_mask, ix1 = (x0 + 1) & px_mask;
    int iy0 = y0 & py_mask, iy1 = (y0 + 1) & py_mask;

    float v00 = Hash2D(ix0, iy0, seed);
    float v10 = Hash2D(ix1, iy0, seed);
    float v01 = Hash2D(ix0, iy1, seed);
    float v11 = Hash2D(ix1, iy1, seed);

    float top = v00 + (v10 - v00) * ux;
    float bot = v01 + (v11 - v01) * ux;
    return top + (bot - top) * uy;
}

float FbmPeriodic512(float x, float y, int cell_size, int octaves, int seed) {
    float val      = 0.0f;
    float amp      = 0.5f;
    float inv_cell = 1.0f / static_cast<float>(cell_size);
    int   period   = std::max(1, 512 / cell_size);

    for (int i = 0; i < octaves; ++i) {
        int mask = period - 1;
        val += amp * SmoothNoisePow2(x * inv_cell, y * inv_cell, mask, mask, seed + i * 17);
        inv_cell *= 2.0f;
        period <<= 1;
        amp *= 0.5f;
    }
    return val;
}

float SoftRidgedPeriodic512(float x, float y, int cell_size, int octaves, int seed) {
    float val      = 0.0f;
    float amp      = 0.5f;
    float inv_cell = 1.0f / static_cast<float>(cell_size);
    int   period   = std::max(1, 512 / cell_size);

    for (int i = 0; i < octaves; ++i) {
        int   mask  = period - 1;
        float n     = SmoothNoisePow2(x * inv_cell, y * inv_cell, mask, mask, seed + i * 29);
        float ridge = 1.0f - std::sqrt((n * 2.0f - 1.0f) * (n * 2.0f - 1.0f) + 0.08f);
        val += ridge * amp;
        inv_cell *= 2.0f;
        period <<= 1;
        amp *= 0.5f;
    }
    return val;
}

void BlurPeriodic512(std::vector<float>& field, int size, int passes) {
    const int          mask = size - 1;
    std::vector<float> tmp(field.size(), 0.0f);

    for (int p = 0; p < passes; ++p) {
        for (int y = 0; y < size; ++y) {
            size_t row = static_cast<size_t>(y) * size;
            for (int x = 0; x < size; ++x) {
                float sum = field[row + ((x - 2) & mask)] * 1.0f +
                            field[row + ((x - 1) & mask)] * 4.0f +
                            field[row + (x & mask)]       * 6.0f +
                            field[row + ((x + 1) & mask)] * 4.0f +
                            field[row + ((x + 2) & mask)] * 1.0f;
                tmp[row + x] = sum * (1.0f / 16.0f);
            }
        }
        for (int y = 0; y < size; ++y) {
            size_t r_m2 = static_cast<size_t>((y - 2) & mask) * size;
            size_t r_m1 = static_cast<size_t>((y - 1) & mask) * size;
            size_t r_0  = static_cast<size_t>(y) * size;
            size_t r_p1 = static_cast<size_t>((y + 1) & mask) * size;
            size_t r_p2 = static_cast<size_t>((y + 2) & mask) * size;
            for (int x = 0; x < size; ++x) {
                float sum = tmp[r_m2 + x] * 1.0f +
                            tmp[r_m1 + x] * 4.0f +
                            tmp[r_0  + x] * 6.0f +
                            tmp[r_p1 + x] * 4.0f +
                            tmp[r_p2 + x] * 1.0f;
                field[r_0 + x] = sum * (1.0f / 16.0f);
            }
        }
    }
}

float RoundedBoxSdf(float px, float py, float cx, float cy, float half_w, float half_h, float radius) {
    float dx    = std::fabs(px - cx) - (half_w - radius);
    float dy    = std::fabs(py - cy) - (half_h - radius);
    float out_d = std::hypot(std::max(dx, 0.0f), std::max(dy, 0.0f));
    float in_d  = std::min(std::max(dx, dy), 0.0f);
    return out_d + in_d - radius;
}

struct BaseSurfaceData {
    std::vector<uint8_t> rgba;
    int                  size_mask = 511;
};

const BaseSurfaceData& GetBaseSurface(WindowPreset preset) {
    static BaseSurfaceData s_surfaces[2];
    const int              idx = (preset == WindowPreset::Paper) ? 1 : 0;
    BaseSurfaceData&       out = s_surfaces[idx];

    if (!out.rgba.empty()) {
        return out;
    }

    const SurfaceSpec&    spec       = GetSurfaceSpec(preset);
    std::filesystem::path custom_png = LocalizeFindFile(spec.plate_path);
    int                   png_w = 0, png_h = 0;
    std::vector<uint8_t>  loaded;
    if (!custom_png.empty() && imagefile::LoadPng(custom_png, png_w, png_h, loaded) &&
        png_w == png_h && (png_w == 256 || png_w == 512)) {
        out.size_mask = png_w - 1;
        out.rgba      = std::move(loaded);
        return out;
    }

    constexpr int kSize = 512;
    constexpr int kMask = kSize - 1;
    out.size_mask       = kMask;

    const int          s = spec.seed_offset;
    std::vector<float> height_map(static_cast<size_t>(kSize) * kSize, 0.0f);
    std::vector<float> tone_map(static_cast<size_t>(kSize) * kSize, 0.0f);

    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            float fx = static_cast<float>(x);
            float fy = static_cast<float>(y);

            float wx = (FbmPeriodic512(fx, fy, 64, 3, s + 501) - 0.5f) * 14.0f;
            float wy = (FbmPeriodic512(fx, fy, 64, 3, s + 601) - 0.5f) * 14.0f;

            float macro  = FbmPeriodic512(fx + wx, fy + wy, 128, 4, s + 101);
            float med    = FbmPeriodic512(fx - wy * 0.5f, fy + wx * 0.5f, 64, 4, s + 303);
            float detail = FbmPeriodic512(fx, fy, 32, 3, s + 202);
            float soft_r = SoftRidgedPeriodic512(fx + wx * 0.4f, fy - wy * 0.4f, 64, 2, s + 404);

            size_t i      = static_cast<size_t>(y) * kSize + x;
            height_map[i] = macro * 0.35f + med * 0.38f + detail * 0.15f + soft_r * 0.12f;
            tone_map[i]   = (macro - 0.48f) * spec.macro_weight +
                            (med - 0.48f) * spec.med_weight +
                            (detail - 0.47f) * spec.detail_weight +
                            (soft_r - 0.32f) * spec.ridge_weight;
        }
    }

    BlurPeriodic512(height_map, kSize, 2);
    BlurPeriodic512(tone_map, kSize, 1);

    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            float fx    = static_cast<float>(x);
            float fy    = static_cast<float>(y);
            float tooth = SmoothNoisePow2(fx * 0.5f, fy * 0.5f, 255, 255, s + 808) * 0.65f +
                          Hash2D(x, y, s + 909) * 0.35f;
            height_map[static_cast<size_t>(y) * kSize + x] += tooth * spec.tooth_weight;
        }
    }

    static constexpr float kBayer4x4[4][4] = {
        {-0.46875f,  0.03125f, -0.34375f,  0.15625f},
        { 0.28125f, -0.21875f,  0.40625f, -0.09375f},
        {-0.28125f,  0.21875f, -0.40625f,  0.09375f},
        { 0.46875f, -0.03125f,  0.34375f, -0.15625f}
    };

    out.rgba.assign(static_cast<size_t>(kSize) * kSize * 4, 255);

    for (int y = 0; y < kSize; ++y) {
        int ym1 = (y - 1) & kMask;
        int yp1 = (y + 1) & kMask;
        for (int x = 0; x < kSize; ++x) {
            int xm1 = (x - 1) & kMask;
            int xp1 = (x + 1) & kMask;

            size_t i    = static_cast<size_t>(y) * kSize + x;
            float  h_tl = height_map[static_cast<size_t>(ym1) * kSize + xm1];
            float  h_br = height_map[static_cast<size_t>(yp1) * kSize + xp1];
            float  h_t  = height_map[static_cast<size_t>(ym1) * kSize + x];
            float  h_b  = height_map[static_cast<size_t>(yp1) * kSize + x];

            float bump   = ((h_tl - h_br) * 0.7f + (h_t - h_b) * 0.3f) * spec.bump_strength;
            float dither = kBayer4x4[y & 3][x & 3] * spec.dither_strength;
            float delta  = tone_map[i] + bump + dither;

            float r = spec.base_r + delta * spec.scale_r;
            float g = spec.base_g + delta * spec.scale_g;
            float b = spec.base_b + delta * spec.scale_b;

            size_t out_idx        = i * 4;
            out.rgba[out_idx + 0] = static_cast<uint8_t>(std::lround(std::clamp(r, 0.0f, 255.0f) / 3.0f) * 3);
            out.rgba[out_idx + 1] = static_cast<uint8_t>(std::lround(std::clamp(g, 0.0f, 255.0f) / 3.0f) * 3);
            out.rgba[out_idx + 2] = static_cast<uint8_t>(std::lround(std::clamp(b, 0.0f, 255.0f) / 3.0f) * 3);
            out.rgba[out_idx + 3] = 255;
        }
    }

    return out;
}

struct CachedWindowTex {
    CTexture tex{};
    unsigned tbp = 0;
};

CTexture* GetProceduralWindowTex(int width_px, int height_px, WindowPreset preset) {
    using CacheKey = std::tuple<int, int, int>;
    static std::map<CacheKey, CachedWindowTex> s_cache;

    const CacheKey key{width_px, height_px, static_cast<int>(preset)};
    auto           it = s_cache.find(key);
    if (it != s_cache.end() && it->second.tbp != 0 && PortKeyLive(it->second.tbp)) {
        return &it->second.tex;
    }

    const SurfaceSpec&     spec        = GetSurfaceSpec(preset);
    const BaseSurfaceData& surface     = GetBaseSurface(preset);
    const int              size_mask   = surface.size_mask;
    const int              base_stride = size_mask + 1;

    PortDecodedTexture decoded{};
    decoded.width     = static_cast<unsigned>(width_px);
    decoded.height    = static_cast<unsigned>(height_px);
    decoded.format    = gfx::TextureFormat::Rgba8;
    decoded.has_alpha = true;

    std::vector<uint8_t> rgba(static_cast<size_t>(width_px) * height_px * 4, 0);

    const float     cx      = width_px * 0.5f;
    const float     cy      = height_px * 0.5f;
    const float     half_w  = width_px * 0.5f - 1.5f;
    const float     half_h  = height_px * 0.5f - 1.5f;
    constexpr float kRadius = 11.0f;
    constexpr float kBevelW = 12.0f;
    const float     inv_w   = 1.0f / static_cast<float>(std::max(1, width_px));
    const float     inv_h   = 1.0f / static_cast<float>(std::max(1, height_px));

    for (int y = 0; y < height_px; ++y) {
        const int   by          = y & size_mask;
        const bool  near_y_edge = (y < 16 || y >= height_px - 16);
        const float v_norm      = static_cast<float>(y) * inv_h;

        for (int x = 0; x < width_px; ++x) {
            const int    bx      = x & size_mask;
            const size_t src_idx = (static_cast<size_t>(by) * base_stride + bx) * 4;
            const size_t dst_idx = (static_cast<size_t>(y) * width_px + x) * 4;

            const float u_norm  = static_cast<float>(x) * inv_w;
            const float ambient = (0.45f - (u_norm * 0.45f + v_norm * 0.55f)) * 14.0f;

            if (!near_y_edge && x >= 16 && x < width_px - 16) {
                rgba[dst_idx + 0] = static_cast<uint8_t>(std::clamp(static_cast<int>(surface.rgba[src_idx + 0] + ambient * spec.scale_r), 0, 255));
                rgba[dst_idx + 1] = static_cast<uint8_t>(std::clamp(static_cast<int>(surface.rgba[src_idx + 1] + ambient * spec.scale_g), 0, 255));
                rgba[dst_idx + 2] = static_cast<uint8_t>(std::clamp(static_cast<int>(surface.rgba[src_idx + 2] + ambient * spec.scale_b), 0, 255));
                rgba[dst_idx + 3] = 255;
                continue;
            }

            float px   = static_cast<float>(x) + 0.5f;
            float py   = static_cast<float>(y) + 0.5f;
            float dist = RoundedBoxSdf(px, py, cx, cy, half_w, half_h, kRadius);
            if (dist > 0.5f) {
                continue;
            }

            float edge_alpha = std::clamp(0.5f - dist, 0.0f, 1.0f);
            float inward     = -dist;

            float r = static_cast<float>(surface.rgba[src_idx + 0]) + ambient * spec.scale_r;
            float g = static_cast<float>(surface.rgba[src_idx + 1]) + ambient * spec.scale_g;
            float b = static_cast<float>(surface.rgba[src_idx + 2]) + ambient * spec.scale_b;

            if (inward < kBevelW) {
                constexpr float eps = 0.5f;
                float gx = RoundedBoxSdf(px + eps, py, cx, cy, half_w, half_h, kRadius) -
                           RoundedBoxSdf(px - eps, py, cx, cy, half_w, half_h, kRadius);
                float gy = RoundedBoxSdf(px, py + eps, cx, cy, half_w, half_h, kRadius) -
                           RoundedBoxSdf(px, py - eps, cx, cy, half_w, half_h, kRadius);
                float glen = std::max(1e-5f, std::hypot(gx, gy));
                float nx = gx / glen, ny = gy / glen;

                float light_dot    = -(nx * 0.65f + ny * 0.76f);
                float profile      = std::sin(std::min(1.0f, inward / kBevelW) * 3.14159265f);
                float inner_groove = std::exp(-((inward - 9.5f) * (inward - 9.5f)) / 3.5f) * 6.0f;
                float bevel        = light_dot * profile * spec.bevel_strength - inner_groove;

                r += bevel * spec.bevel_r;
                g += bevel * spec.bevel_g;
                b += bevel * spec.bevel_b;

                if (inward < 1.8f) {
                    float rim_t = std::clamp((1.8f - inward) / 1.8f, 0.0f, 1.0f);
                    r = r * (1.0f - rim_t * 0.62f) + spec.rim_r * rim_t;
                    g = g * (1.0f - rim_t * 0.62f) + spec.rim_g * rim_t;
                    b = b * (1.0f - rim_t * 0.62f) + spec.rim_b * rim_t;
                }
            }

            rgba[dst_idx + 0] = static_cast<uint8_t>(std::lround(std::clamp(r, 0.0f, 255.0f)));
            rgba[dst_idx + 1] = static_cast<uint8_t>(std::lround(std::clamp(g, 0.0f, 255.0f)));
            rgba[dst_idx + 2] = static_cast<uint8_t>(std::lround(std::clamp(b, 0.0f, 255.0f)));
            rgba[dst_idx + 3] = static_cast<uint8_t>(std::lround(edge_alpha * 255.0f));
        }
    }

    decoded.levels.push_back(std::move(rgba));
    unsigned tbp = PortCreateTexture(decoded, PortTextureOwner::Other, nullptr);
    if (tbp == 0) {
        return nullptr;
    }

    CachedWindowTex& entry = s_cache[key];
    entry.tbp              = tbp;
    entry.tex.Initialize();
    std::strcpy(entry.tex.name, spec.tex_name);
    entry.tex.width  = width_px;
    entry.tex.height = height_px;
    entry.tex.bpp    = 4;
    entry.tex.block  = -1;
    entry.tex.tex0   = static_cast<u_long>(tbp) | (1ull << 34);
    entry.tex.tex1   = 0;
    return &entry.tex;
}

} // namespace

void TrackerWindow::Draw(int x, int y, int width_px, int height_px, int alpha, WindowPreset preset) {
    if (preset == WindowPreset::None || width_px <= 0 || height_px <= 0 || alpha <= 0) {
        return;
    }
    if (preset == WindowPreset::DarkTranslucent) {
        HelpBox::Draw(x, y, width_px, height_px, alpha);
        return;
    }
    if (preset == WindowPreset::Recessed) {
        DrawIndentation(Rect(x, y, width_px, height_px), true, alpha);
        return;
    }

    CTexture* surface = (width_px >= 48 && height_px >= 48)
                        ? GetProceduralWindowTex(width_px, height_px, preset)
                        : nullptr;
    if (surface == nullptr) {
        float w = std::max(0.0f, (width_px - 28) / 16.0f);
        float h = std::max(0.0f, (height_px - 76) / 16.0f);
        MenuHelpWinDraw(x, y, w, h, alpha);
        return;
    }

    setbilinear(0);
    DrawMenu2DSprite(surface,
                     CRect_i_(x, y, width_px, height_px),
                     CRect_i_(0, 0, width_px, height_px),
                     alpha);
}

void TrackerWindow::DrawIndentation(const Rect& box, bool dark_variant, int alpha) {
    if (box.Empty() || alpha <= 0) return;

    CTexture* tex = detail::GetWhitePixelTex();
    if (tex == nullptr) return;

    const int rim_light_a  = (alpha * (dark_variant ? 42 : 55)) >> 7;
    const int rim_shadow_a = (alpha * (dark_variant ? 95 : 70)) >> 7;
    const int mid_shadow_a = (alpha * (dark_variant ? 75 : 52)) >> 7;
    const int floor_a      = (alpha * (dark_variant ? 56 : 36)) >> 7;

    detail::DrawRoundedRect(tex, box.x + 1, box.y + 1, box.w, box.h, 255, 248, 230, rim_light_a, true);
    detail::DrawRoundedRect(tex, box.x, box.y, box.w - 1, box.h - 1, 0, 0, 0, rim_shadow_a, true);

    if (box.w > 4 && box.h > 4) {
        detail::DrawRoundedRect(tex, box.x + 1, box.y + 1, box.w - 2, box.h - 2, 0, 0, 0, mid_shadow_a, true);
    }

    if (box.w > 6 && box.h > 6) {
        detail::DrawRoundedRect(tex, box.x + 2, box.y + 2, box.w - 3, box.h - 3, 0, 0, 0, floor_a, false);
    }
}

void TrackerWindow::DrawStoneBanner(const Rect& box, int alpha, int brightness,
                                    std::string_view title, const TextStyle& title_style) {
    if (box.Empty() || alpha <= 0) return;

    CTexture* wep_tex = detail::GetWepStatusTex();
    if (wep_tex == nullptr) {
        DrawIndentation(box, false, alpha);
    } else {
        setbilinear(0);
        const int left_w  = std::min(40, box.w / 3);
        const int right_w = std::min(32, box.w / 3);
        const int mid_w   = std::max(0, box.w - left_w - right_w);

        DrawMenu2DSprite(wep_tex, CRect_i_(box.x, box.y, left_w, box.h),
                         CRect_i_(0xE0, 0x1A8, 0x28, 0x20), brightness, brightness, brightness, alpha);
        if (mid_w > 0) {
            DrawMenu2DSprite(wep_tex, CRect_i_(box.x + left_w, box.y, mid_w, box.h),
                             CRect_i_(0x108, 0x1A8, 0x78, 0x20), brightness, brightness, brightness, alpha);
        }
        if (right_w > 0) {
            DrawMenu2DSprite(wep_tex, CRect_i_(box.x + left_w + mid_w, box.y, right_w, box.h),
                             CRect_i_(0xD4, 0x1D4, 0x20, 0x20), brightness, brightness, brightness, alpha);
        }
    }

    if (!title.empty()) {
        MenuTextureReload(0x1A);
        Label lbl(title, title_style);
        lbl.Draw(box.CenterX(), box.y + 1, alpha);
    }
}

void HelpBox::Draw(int x, int y, int width_px, int height_px, int alpha) {
    setbilinear(0);
    MenuTextureReload(BtlMenuReadBlock);
    float w = std::max(0.0f, (width_px - 48) / 16.0f);
    float h = std::max(0.0f, (height_px - 44) / 16.0f);
    MenuHelpWinDraw(x, y, w, h, alpha);
}

void HelpBox::DrawTitled(int box_x, int bottom_y, int box_w, int text_offset_x,
                         std::string_view title, std::string_view desc,
                         int alpha, int line_height) {
    if (title.empty() || desc.empty() || alpha <= 0) return;

    MenuTextureReload(0x1A);
    const int                draw_x     = box_x + (text_offset_x > 0 ? text_offset_x : 22);
    const int                max_text_w = box_w - 46;
    std::vector<std::string> lines      = WrapText(desc, max_text_w);
    const int                num_lines  = std::max(1, static_cast<int>(lines.size()));
    const int                box_h      = 32 + (num_lines * line_height);
    const int                box_y      = bottom_y - box_h;

    Draw(box_x, box_y, box_w, box_h, alpha);

    MenuTextureReload(0x1A);
    const int title_y = box_y + 12;
    const int desc_y  = title_y + 24;

    Label(title, ColorWhite).Draw(draw_x, title_y, alpha);
    for (size_t i = 0; i < lines.size(); ++i) {
        Label(lines[i], ColorWhite).Draw(draw_x, desc_y + static_cast<int>(i) * line_height, alpha);
    }
}

void Window::Draw(int x, int y, int width_px, int height_px, int alpha, WindowPreset preset) {
    TrackerWindow::Draw(x, y, width_px, height_px, alpha, preset);
}

void TabPlate::Draw(int left, int right, int y, int alpha) {
    constexpr int kPlateSide   = 24;
    constexpr int kPlateHeight = 44;
    float         middle       = std::max(0.0f, static_cast<float>(right - left + 28 - 2 * kPlateSide) / 16.0f);
    MenuHelpWinDraw(left - 14, y - (kPlateHeight - 20) / 2, middle, 0.0f, alpha);
}

void Divider::DrawVertical(int x, int top_y, int bottom_y, int alpha) {
    CTexture* tex = detail::GetWhitePixelTex();
    if (tex == nullptr || bottom_y <= top_y) return;
    CRect_i_  uv_white(0x6A, 0x6E, 1, 1);
    const int h = bottom_y - top_y;

    DrawMenu2DSprite(tex, CRect_i_(x - 1, top_y,     1, h),     uv_white, 0,   0,   0,   (alpha * 50) >> 7);
    DrawMenu2DSprite(tex, CRect_i_(x,     top_y,     1, h),     uv_white, 0,   0,   0,   (alpha * 95) >> 7);
    DrawMenu2DSprite(tex, CRect_i_(x + 1, top_y,     1, h),     uv_white, 0,   0,   0,   (alpha * 65) >> 7);
    DrawMenu2DSprite(tex, CRect_i_(x + 2, top_y + 1, 1, h - 1), uv_white, 255, 248, 230, (alpha * 48) >> 7);
}

void Divider::DrawHorizontal(int left_x, int right_x, int y, int alpha) {
    CTexture* tex = detail::GetWhitePixelTex();
    if (tex == nullptr || right_x <= left_x) return;
    CRect_i_  uv_white(0x6A, 0x6E, 1, 1);
    const int w = right_x - left_x;

    DrawMenu2DSprite(tex, CRect_i_(left_x,     y - 1, w,     1), uv_white, 0,   0,   0,   (alpha * 50) >> 7);
    DrawMenu2DSprite(tex, CRect_i_(left_x,     y,     w,     1), uv_white, 0,   0,   0,   (alpha * 95) >> 7);
    DrawMenu2DSprite(tex, CRect_i_(left_x,     y + 1, w,     1), uv_white, 0,   0,   0,   (alpha * 65) >> 7);
    DrawMenu2DSprite(tex, CRect_i_(left_x + 1, y + 2, w - 1, 1), uv_white, 255, 248, 230, (alpha * 48) >> 7);
}

void SubWindow::DrawBackground(int alpha) const {
    if (m_preset != WindowPreset::None) {
        TrackerWindow::Draw(m_outer, alpha, m_preset);
    }
}

void SubWindow::DrawBanner(std::string_view title, int banner_w, int banner_h,
                           int y_offset, int alpha) const {
    Rect banner_box = m_outer.Anchored(Anchor::TopCenter, banner_w, banner_h, 0, y_offset);
    TrackerWindow::DrawStoneBanner(banner_box, alpha, 0x80, title);
}

void SubWindow::DrawAnchoredLabel(std::string_view text, Anchor anchor, const TextStyle& style,
                                  int dx, int dy, int alpha) const {
    Label lbl(text, style);
    lbl.DrawAnchored(m_content, anchor, dx, dy, alpha);
}

std::pair<SubWindow, SubWindow> SubWindow::SplitVertical(int left_w, int gap,
                                                         bool draw_divider, int alpha) const {
    int divider_x = 0;
    auto [left_r, right_r] = m_content.SplitVertical(left_w, gap, &divider_x);
    if (draw_divider) {
        Divider::DrawVertical(divider_x, m_content.Top(), m_content.Bottom(), alpha);
    }
    return {SubWindow(left_r), SubWindow(right_r)};
}

std::pair<SubWindow, SubWindow> SubWindow::SplitHorizontal(int top_h, int gap,
                                                           bool draw_divider, int alpha) const {
    int divider_y = 0;
    auto [top_r, bot_r] = m_content.SplitHorizontal(top_h, gap, &divider_y);
    if (draw_divider) {
        Divider::DrawHorizontal(m_content.Left(), m_content.Right(), divider_y, alpha);
    }
    return {SubWindow(top_r), SubWindow(bot_r)};
}

} // namespace UI
