#include "ui_framework.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <tuple>
#include <utility>

#include "battlemenu.hpp"
#include "caption_text.hpp"
#include "captionfont.hpp"
#include "clsmes.hpp"
#include "draw2d_port.hpp"
#include "gameutil.hpp"
#include "gfx/gfx.hpp"
#include "itemdata.hpp"
#include "localize.hpp"
#include "memcard.hpp"
#include "menu_draw.hpp"
#include "menu_inventory.hpp"
#include "menuetc.hpp"
#include "mglib.hpp"
#include "platform/config.hpp"
#include "platform/imagefile.hpp"
#include "texture.hpp"
#include "texture_port.hpp"
#include "textwipe.hpp"

extern char AtoraVibeTextureName[];
extern void MenuHelpWinDraw(int x, int y, float w, float h, int alpha);
extern void setbilinear(int n);

namespace UI {

namespace {

constexpr float kBaseCapHeight = 14.5f;

struct ScopedShadow {
    int  saved_shadow = 0;
    bool active       = false;

    explicit ScopedShadow(bool enable_shadow) {
        if (!enable_shadow) {
            Config& cfg  = const_cast<Config&>(ConfigGet());
            saved_shadow = cfg.text_shadow;
            cfg.text_shadow = 0;
            active = true;
        }
    }

    ~ScopedShadow() {
        if (active) {
            Config& cfg     = const_cast<Config&>(ConfigGet());
            cfg.text_shadow = saved_shadow;
        }
    }
};

CTexture* FindMenuTexture(const char* name, CTexture* cached = nullptr) {
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

CTexture* GetWhitePixelTex() { return FindMenuTexture("gaiji"); }
CTexture* GetPerBoardTex()   { return FindMenuTexture("perbrd", PerBoardTex); }
CTexture* GetWepStatusTex()  { return FindMenuTexture("wepstatus", WepStatus); }

// -----------------------------------------------------------------------------
// Parameterized Procedural Surface Generator (shared by BlueStone & Paper)
// -----------------------------------------------------------------------------
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

            size_t i        = static_cast<size_t>(y) * kSize + x;
            height_map[i]   = macro * 0.35f + med * 0.38f + detail * 0.15f + soft_r * 0.12f;
            tone_map[i]     = (macro - 0.48f) * spec.macro_weight +
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

void DrawRoundedRect(CTexture* tex, int x, int y, int w, int h,
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
            out[0] = 240; out[1] = 238; out[2] = 232;
            break;
    }
}

std::u32string DecodeUtf8(std::string_view in) {
    std::u32string out;
    for (size_t i = 0; i < in.size();) {
        const unsigned char c     = static_cast<unsigned char>(in[i]);
        const int           extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1
                                                 : (c >> 4) == 14 ? 2
                                                 : (c >> 3) == 30 ? 3 : -1;
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

bool UseScaledRenderer(std::string_view text, float scale, float xscale, FontWeight weight) {
    if (std::abs(scale - 1.0f) <= 0.001f &&
        std::abs(xscale - 1.0f) <= 0.001f &&
        weight == FontWeight::Normal) {
        return false;
    }
    if (text.find('{') != std::string::npos) {
        return false;
    }
    return caption::Ready();
}

int MeasureScaledText(std::string_view text, float scale, float xscale, FontWeight weight) {
    if (text.empty()) return 0;

    struct VariantGuard {
        explicit VariantGuard(bool thin) { capfont::UseVariant(thin); }
        ~VariantGuard() { capfont::UseVariant(false); }
    } guard(weight == FontWeight::Thin && caption::ThinReady());

    const gfx::LogicalMapping mapping    = gfx::GetUiMapping(gfx::CurrentRenderTarget());
    const capfont::Metrics    metrics    = capfont::GetMetrics();
    const float               cap_height = kBaseCapHeight * scale;
    const float               em         = cap_height / std::max(0.2f, metrics.cap);
    const int                 em_px      = std::clamp(static_cast<int>(em * mapping.scale_x + 0.5f), 4, 512);
    const float               gap        = cap_height * 0.075f;

    float natural = 0.0f;
    int   drawn   = 0;
    for (char32_t ch : DecodeUtf8(text)) {
        capfont::Glyph glyph;
        if (ch == U' ') {
            natural += cap_height * 0.32f + (3.0f * scale);
            drawn++;
        } else if (capfont::GetGlyph(ch, em_px, glyph) && glyph.width > 0) {
            natural += (glyph.width / mapping.scale_x * xscale) + gap;
            drawn++;
        }
    }
    if (drawn > 0) {
        natural -= gap;
    }
    return static_cast<int>(std::round(natural));
}

void DrawScaledText(std::string_view text, float center_x, float cap_top, float scale, float xscale,
                    u32 color, FontWeight weight, bool drop_shadow, int alpha) {
    if (text.empty() || alpha <= 0) return;

    const float cap_height = kBaseCapHeight * scale;
    const float rows       = draw2d::RowScale();
    const float fade       = std::clamp(static_cast<float>(alpha) / 128.0f, 0.0f, 1.0f);

    caption::LineStyle style{};
    style.thin    = (weight == FontWeight::Thin && caption::ThinReady());
    style.weight  = (weight == FontWeight::Bold) ? 1.1f : 0.0f;
    style.xscale  = xscale;
    style.word    = 3.0f * scale;
    style.ink_fit = false;
    style.edge_width = 0.0f;

    if (drop_shadow && ConfigGet().text_shadow > 0) {
        caption::LineStyle shadow_style = style;
        shadow_style.fill[0]    = 0;
        shadow_style.fill[1]    = 0;
        shadow_style.fill[2]    = 0;
        shadow_style.edge[0]    = 0;
        shadow_style.edge[1]    = 0;
        shadow_style.edge[2]    = 0;
        shadow_style.edge_width = 0.8f;
        shadow_style.opacity    = std::clamp(static_cast<float>(ConfigGet().text_shadow) / 100.0f, 0.0f, 1.0f);

        const float s_dx = 2.0f * scale;
        const float s_dy = 2.0f * scale;
        caption::DrawLine(text, center_x + s_dx, cap_top + s_dy, cap_height, 1.0f, 0.0f, shadow_style, fade, rows, nullptr);
    }

    ColorToRgb(color, style.fill);
    style.opacity = 1.0f;
    caption::DrawLine(text, center_x, cap_top, cap_height, 1.0f, 0.0f, style, fade, rows, nullptr);
}

} // namespace

std::vector<std::string> WrapText(std::string_view text, int max_width) {
    std::vector<std::string> lines;
    if (text.empty() || max_width <= 0) return lines;

    std::string current_line;
    size_t      i = 0;

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
// Windows, Banners, Indentations & Dividers
// -----------------------------------------------------------------------------
void TrackerWindow::Draw(int x, int y, int width_px, int height_px, int alpha, WindowPreset preset) {
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
    setbilinear(1);
}

void TrackerWindow::DrawIndentation(const Rect& box, bool dark_variant, int alpha) {
    if (box.Empty() || alpha <= 0) return;

    CTexture* perbrd = GetPerBoardTex();
    if (perbrd == nullptr) return;

    setbilinear(0);
    constexpr int src_y   = 0x80;
    const int     left_w  = std::min(3, box.w / 3);
    const int     right_w = std::min(8, box.w - left_w);
    const int     mid_w   = std::max(0, box.w - left_w - right_w);

    const u8 r = dark_variant ? 0x46 : 0x80;
    const u8 g = dark_variant ? 0x50 : 0x80;
    const u8 b = dark_variant ? 0x64 : 0x80;

    DrawMenu2DSprite(perbrd, CRect_i_(box.x, box.y, left_w, box.h),
                     CRect_i_(0x20, src_y, 3, 0x18), r, g, b, alpha);

    int cur_x = box.x + left_w;
    int rem_w = mid_w;
    while (rem_w > 0) {
        int seg_w = std::min(32, rem_w);
        DrawMenu2DSprite(perbrd, CRect_i_(cur_x, box.y, seg_w, box.h),
                         CRect_i_(0x40, src_y, seg_w, 0x18), r, g, b, alpha);
        cur_x += seg_w;
        rem_w -= seg_w;
    }

    if (right_w > 0) {
        DrawMenu2DSprite(perbrd, CRect_i_(box.x + left_w + mid_w, box.y, right_w, box.h),
                         CRect_i_(0x78, src_y, 0x08, 0x18), r, g, b, alpha);
    }
    setbilinear(1);
}

void TrackerWindow::DrawStoneBanner(const Rect& box, int alpha, int brightness,
                                    std::string_view title, const TextStyle& title_style) {
    if (box.Empty() || alpha <= 0) return;

    CTexture* wep_tex = GetWepStatusTex();
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
        setbilinear(1);
    }

    if (!title.empty()) {
        MenuTextureReload(0x1A);
        Label lbl(title, title_style);
        lbl.Draw(box.CenterX(), box.y + 1, alpha);
    }
}

void HelpBox::Draw(int x, int y, int width_px, int height_px, int alpha) {
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
    CTexture* tex = GetWhitePixelTex();
    if (tex == nullptr || bottom_y <= top_y) return;
    CRect_i_ uv_white(0x6A, 0x6E, 1, 1);
    int      h = bottom_y - top_y;
    DrawMenu2DSprite(tex, CRect_i_(x, top_y, 1, h), uv_white, 34, 42, 56, (alpha * 105) >> 7);
    DrawMenu2DSprite(tex, CRect_i_(x + 1, top_y, 1, h), uv_white, 134, 150, 174, (alpha * 85) >> 7);
}

void Divider::DrawHorizontal(int left_x, int right_x, int y, int alpha) {
    CTexture* tex = GetWhitePixelTex();
    if (tex == nullptr || right_x <= left_x) return;
    CRect_i_ uv_white(0x6A, 0x6E, 1, 1);
    int      w = right_x - left_x;
    DrawMenu2DSprite(tex, CRect_i_(left_x, y, w, 1), uv_white, 34, 42, 56, (alpha * 105) >> 7);
    DrawMenu2DSprite(tex, CRect_i_(left_x, y + 1, w, 1), uv_white, 134, 150, 174, (alpha * 85) >> 7);
}

// -----------------------------------------------------------------------------
// Label & Counter Helpers
// -----------------------------------------------------------------------------
Label::Label(std::string_view text, u32 color, Alignment align, bool drop_shadow,
             float scale, FontWeight weight, float xscale)
    : m_raw_text(text),
      m_color(color),
      m_align(align),
      m_drop_shadow(drop_shadow),
      m_scale(scale),
      m_xscale(xscale),
      m_weight(weight) {
    m_text.Set(m_raw_text.c_str());
}

Label::Label(std::string_view text, const TextStyle& style)
    : m_raw_text(text),
      m_color(style.color),
      m_align(style.align),
      m_drop_shadow(style.drop_shadow),
      m_scale(style.scale),
      m_xscale(style.xscale),
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

void Label::SetColor(u32 color)          { m_color = color; }
void Label::SetAlignment(Alignment align){ m_align = align; }
void Label::SetDropShadow(bool enabled)  { m_drop_shadow = enabled; }
void Label::SetScale(float scale)        { m_scale = scale; m_dirty = true; }
void Label::SetXScale(float xscale)      { m_xscale = xscale; m_dirty = true; }
void Label::SetWeight(FontWeight weight) { m_weight = weight; m_dirty = true; }
void Label::SetOffset(int dx, int dy)    { m_dx = dx; m_dy = dy; }

void Label::SetStyle(const TextStyle& style) {
    m_color       = style.color;
    m_align       = style.align;
    m_drop_shadow = style.drop_shadow;
    m_scale       = style.scale;
    m_xscale      = style.xscale;
    m_weight      = style.weight;
    m_dx          = style.text_dx;
    m_dy          = style.text_dy;
    m_dirty       = true;
}

int Label::GetWidth() {
    if (UseScaledRenderer(m_raw_text, m_scale, m_xscale, m_weight)) {
        m_cached_width = MeasureScaledText(m_raw_text, m_scale, m_xscale, m_weight);
        m_dirty        = false;
        return m_cached_width;
    }
    m_text.RefreshGlyphs();
    m_cached_width = m_text.Width();
    m_dirty        = false;
    return m_cached_width;
}

void Label::Draw(int x, int y, int alpha) {
    int draw_x = x + m_dx;
    int draw_y = y + m_dy;
    int width  = GetWidth();

    if (UseScaledRenderer(m_raw_text, m_scale, m_xscale, m_weight)) {
        float center_x = static_cast<float>(draw_x);
        if (m_align == Alignment::Left) {
            center_x = static_cast<float>(draw_x) + static_cast<float>(width) * 0.5f;
        } else if (m_align == Alignment::Right) {
            center_x = static_cast<float>(draw_x) - static_cast<float>(width) * 0.5f;
        }
        float cap_height = kBaseCapHeight * m_scale;
        float cap_top    = static_cast<float>(draw_y) + 3.0f + (kBaseCapHeight - cap_height) * 0.5f;
        DrawScaledText(m_raw_text, center_x, cap_top, m_scale, m_xscale, m_color, m_weight, m_drop_shadow, alpha);
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
    if (UseScaledRenderer(m_raw_text, m_scale, m_xscale, m_weight)) {
        int   width    = GetWidth();
        float center_x = static_cast<float>(box.x) + static_cast<float>(box.w) * 0.5f;
        if (m_align == Alignment::Left) {
            center_x = static_cast<float>(box.x + 6) + static_cast<float>(width) * 0.5f;
        } else if (m_align == Alignment::Right) {
            center_x = static_cast<float>(box.x + box.w - 6) - static_cast<float>(width) * 0.5f;
        }
        float cap_height = kBaseCapHeight * m_scale;
        float cap_top    = static_cast<float>(box.y) + (static_cast<float>(box.h) - cap_height) * 0.5f - 4.0f;
        DrawScaledText(m_raw_text, center_x + static_cast<float>(m_dx), cap_top + static_cast<float>(m_dy),
                       m_scale, m_xscale, m_color, m_weight, m_drop_shadow, alpha);
        return;
    }

    const textwipe::MessageContext msg_ctx(textwipe::Kind::Message);

    int text_w = GetWidth();
    int draw_x = box.x + 6;
    if (m_align == Alignment::Center) {
        draw_x = box.x + (box.w - text_w) / 2;
    } else if (m_align == Alignment::Right) {
        draw_x = box.x + box.w - text_w - 6;
    }

    int draw_y = box.y + (box.h - 22) / 2;

    ScopedShadow shadow_guard(m_drop_shadow);
    m_text.SetColour(m_color);
    m_text.Draw(draw_x + m_dx, draw_y + m_dy, alpha);
}

void Label::DrawCenteredInRect(int x, int y, int width, int height, int alpha) {
    Alignment prev = m_align;
    m_align        = Alignment::Center;
    DrawInRect(Rect(x, y, width, height), alpha);
    m_align = prev;
}

void Label::DrawWithStroke(int x, int y, u32 fill_color, u32 stroke_color, int stroke_px, int alpha) {
    int draw_x = x + m_dx;
    int draw_y = y + m_dy;
    int width  = GetWidth();

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
        Label line_lbl(lines[i], m_color, m_align, m_drop_shadow, m_scale, m_weight, m_xscale);
        line_lbl.SetOffset(m_dx, m_dy);
        line_lbl.Draw(x, y + static_cast<int>(i) * line_height, alpha);
    }
}

void DrawTextBox(std::string_view text, const Rect& box, const TextStyle& style, int alpha) {
    Label lbl(text, style);
    lbl.DrawInRect(box, alpha);
}

void DrawCounter(int x, int y, int current_val, int max_val,
                 float scale, int alpha, u32 normal_color, u32 complete_color) {
    const u32 col = (current_val == max_val && max_val > 0) ? complete_color : normal_color;
    Label("[", col, Alignment::Left, true, scale).Draw(x, y, alpha);
    Label(std::to_string(current_val), col, Alignment::Right, true, scale).Draw(x + 28, y, alpha);
    Label("/", col, Alignment::Center, true, scale).Draw(x + 38, y, alpha);
    Label(std::to_string(max_val), col, Alignment::Right, true, scale).Draw(x + 73, y, alpha);
    Label("]", col, Alignment::Right, true, scale).Draw(x + 82, y, alpha);
}

// -----------------------------------------------------------------------------
// ItemIcon: Draws recessed "wepstatus" stone socket + item icon
// -----------------------------------------------------------------------------
void ItemIcon::DrawSocket(int x, int y, int size, bool filled, int alpha) {
    CTexture* wep_tex = GetWepStatusTex();
    if (wep_tex == nullptr || size <= 0) return;

    setbilinear(0);
    int      src_y = filled ? 0x10A : 0xE0;
    CRect_i_ src(0xD4, src_y, 0x2A, 0x2A);
    DrawMenu2DSprite(wep_tex, CRect_i_(x + 1, y + 2, size, size), src, 0, 0, 0, (alpha * 80) >> 7);
    DrawMenu2DSprite(wep_tex, CRect_i_(x, y, size, size), src, 0x80, 0x80, 0x80, alpha);
    setbilinear(1);
}

bool ItemIcon::Draw(int item_no, int x, int y, int size, int alpha, bool drop_shadow, bool with_socket) {
    if (item_no <= 0 || size <= 0) return false;

    if (with_socket) {
        DrawSocket(x - 2, y - 2, size + 4, false, alpha);
    }

    COM_ITEM_INFO* info = GetCommonItemInfo(item_no);
    if (info == nullptr || info->icon_index < 0) return false;

    if (::ItemIcon == nullptr) ::ItemIcon = FindMenuTexture("itemicon");
    if (::WepIcon == nullptr)  ::WepIcon  = FindMenuTexture("wepicon");

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
    setbilinear(1);

    return true;
}

// -----------------------------------------------------------------------------
// Cursor
// -----------------------------------------------------------------------------
Cursor::Cursor() {
    m_texture = GetWhitePixelTex();
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
    m_texture = GetWhitePixelTex();
    if (m_texture == nullptr) return;

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
    CRect_i_ screen_rect(static_cast<int>(hand_x), static_cast<int>(hand_y), 0x20, 0x20);

    DrawMenu2DSprite(m_texture, shadow_rect, uv, 0, 0, 0, (alpha * 100) >> 7);
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

    StayTex = FindMenuTexture("stayframe", StayTex);
    if (StayTex != nullptr) {
        int box_w = std::max(16, right - left);
        int box_h = std::max(16, bottom - top);
        DrawMenuWaku(left - 14, top - 6, box_w + 12, box_h - 4, 0, StayTex, alpha);
    }
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
    } else if (CTexture* gaiji_tex = GetWhitePixelTex()) {
        DrawMenu2DSprite(gaiji_tex, CRect_i_(x, track_top, 8, track_height), CRect_i_(0x6A, 0x6E, 1, 1), 25, 28, 30, cur_alpha);
    }

    if (m_thumb_tex != nullptr) {
        DrawMenu2DSprite(m_thumb_tex, CRect_i_(x, thumb_y, 8, thumb_height), CRect_i_(140, 280, 24, 32), cur_alpha);
        DrawMenu2DSprite(m_thumb_tex, CRect_i_(x - 4, top_y - 4, 16, 16), CRect_i_(452, 136, 24, 24), cur_alpha);
        DrawMenu2DSprite(m_thumb_tex, CRect_i_(x - 4, track_top + track_height + 2, 16, 16), CRect_i_(452, 164, 24, 24), cur_alpha);
    }
}

// -----------------------------------------------------------------------------
// BevelButton & ConfirmModal
// -----------------------------------------------------------------------------
void BevelButton::Draw(const Rect& box, std::string_view text,
                       const ButtonStyle& style, int alpha) {
    if (CTexture* gaiji_tex = GetWhitePixelTex()) {
        DrawRoundedRect(gaiji_tex, box.x + 2, box.y + 3, box.w, box.h, 0, 0, 0, (alpha * 70) >> 7, style.rounded);

        if (style.preset == ButtonPreset::Terracotta) {
            DrawRoundedRect(gaiji_tex, box.x,     box.y,     box.w,     box.h,     78,  44, 28, alpha, style.rounded);
            DrawRoundedRect(gaiji_tex, box.x,     box.y,     box.w - 1, box.h - 1, 134, 98, 78, alpha, style.rounded);
            DrawRoundedRect(gaiji_tex, box.x + 1, box.y + 1, box.w - 2, box.h - 2, 124, 82, 60, alpha, style.rounded);
            DrawRoundedRect(gaiji_tex, box.x + 2, box.y + 2, box.w - 3, box.h - 3, 92,  52, 34, alpha, false);
            DrawRoundedRect(gaiji_tex, box.x + 2, box.y + 2, box.w - 4, box.h - 4, 112, 64, 42, alpha, false);
            DrawRoundedRect(gaiji_tex, box.x + 2, box.y + 2, box.w - 5, box.h - 5, 118, 70, 46, alpha, false);
            DrawRoundedRect(gaiji_tex, box.x + 3, box.y + 3, box.w - 6, box.h - 6, 112, 64, 42, alpha, false);
        } else {
            DrawRoundedRect(gaiji_tex, box.x,     box.y,     box.w,     box.h,     34, 48,  38, alpha, style.rounded);
            DrawRoundedRect(gaiji_tex, box.x,     box.y,     box.w - 1, box.h - 1, 92, 122, 98, alpha, style.rounded);
            DrawRoundedRect(gaiji_tex, box.x + 1, box.y + 1, box.w - 2, box.h - 2, 76, 104, 82, alpha, style.rounded);
            DrawRoundedRect(gaiji_tex, box.x + 2, box.y + 2, box.w - 3, box.h - 3, 42, 60,  48, alpha, false);
            DrawRoundedRect(gaiji_tex, box.x + 2, box.y + 2, box.w - 4, box.h - 4, 54, 78,  60, alpha, false);
        }
    }

    if (!text.empty()) {
        MenuTextureReload(0x1A);
        TextStyle txt_style{
            .color       = style.text_color,
            .drop_shadow = style.drop_shadow,
            .align       = style.align,
            .scale       = style.text_scale,
            .xscale      = style.text_xscale,
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

void ConfirmModal::Draw(std::string_view title, std::string_view description,
                        std::string_view yes_label, std::string_view no_label,
                        int selected_choice, Cursor& cursor, int alpha) {
    constexpr int box_w = 260;
    constexpr int box_h = 58;
    constexpr int box_x = (640 - box_w) / 2;
    constexpr int box_y = 164;

    HelpBox::Draw(box_x, box_y, box_w, box_h, alpha);

    MenuTextureReload(0x1A);
    Label(title, ColorWhite).Draw(box_x + 22, box_y + 12, alpha);
    Label(description, ColorWhite).Draw(box_x + 22, box_y + 32, alpha);

    constexpr int btn_w  = 100;
    constexpr int btn_h  = 26;
    constexpr int btn_x  = (640 - btn_w) / 2;
    constexpr int btn_y0 = box_y + box_h + 10;
    constexpr int btn_y1 = btn_y0 + btn_h + 6;

    ButtonStyle btn_style{};
    BevelButton::Draw(Rect(btn_x, btn_y0, btn_w, btn_h), yes_label, btn_style, alpha);
    BevelButton::Draw(Rect(btn_x, btn_y1, btn_w, btn_h), no_label,  btn_style, alpha);

    const int sel_y = (selected_choice == 0) ? btn_y0 : btn_y1;
    StayTex         = FindMenuTexture("stayframe", StayTex);
    if (StayTex != nullptr) {
        DrawMenuWaku(btn_x - 14, sel_y - 6, btn_w + 12, btn_h - 4, 0, StayTex, alpha);
    }

    cursor.SetTarget(static_cast<float>(btn_x - 34), static_cast<float>(sel_y + 3));
    cursor.Draw(alpha);
}

// -----------------------------------------------------------------------------
// TextureButton & ProgressBar
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

void ProgressBar::Draw(const Rect& box, float progress, std::string_view text,
                       const ProgressBarStyle& style, int alpha) {
    const float clamped  = std::clamp(progress, 0.0f, 1.0f);
    const bool  complete = (clamped >= 1.0f);

    TrackerWindow::DrawIndentation(box, true, alpha);

    const int max_fill_w = std::max(0, box.w - 8);
    const int fill_w     = static_cast<int>(std::round(clamped * static_cast<float>(max_fill_w)));

    if (fill_w > 0 && box.h > 6) {
        if (GRADATION_COLOR_INFO2* grad_src = GetGradationColorInfo2(complete ? 4 : 3)) {
            GRADATION_COLOR_INFO2 grad = *grad_src;
            for (int c = 0; c < 4; ++c) {
                grad.colors[c].a = static_cast<u8>((alpha * 115) >> 7);
            }
            CRect_i_ fill_rect(box.x + 4, box.y + 3, fill_w, box.h - 7);
            DrawMenuColorGradation(fill_rect, &grad.colors[0], &grad.colors[1], &grad.colors[2], &grad.colors[3]);
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
        // Direct Label::Draw at (CenterX, box.y - 2) centers the glyphs inside the 22px bar
        // without double-applying DrawInRect's -4.0f baseline offset
        Label lbl(text, txt_style);
        lbl.Draw(box.CenterX(), box.y - 2, alpha);
    }
}

} // namespace UI