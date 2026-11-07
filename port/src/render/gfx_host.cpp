// Answers what game code still asks of a renderer when Noktus does the drawing: where the frame lies in the window, and what's known of each texture and target.
// With DC_RENDERER=kazarov, each of these passes straight on to Kazarov's renderer, whose own versions have Kazarov in front of their names: see kazarov_names.hpp.
// Otherwise it's linked in and starts nothing.
// The layout rules, the matrix inverse and WritePng come from Kazarov's gfx, by TheMoonPeople, under the MIT licence: see text/LICENSE.kazarov.

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>

// This order matters: here gfx.hpp declares only Kazarov's versions, and this file defines the plain names. render/passes.hpp can't come in,
// since it calls the plain names.
#include "render/kazarov_names.hpp"
#include "gfx/displaylist.hpp" // Mat4
#include "gfx/gfx.hpp"
#include "render/kazarov_names_end.hpp"
#include "render/noktus_mirror.hpp"

namespace dc::render {
void CountDraw();
}

namespace gfx {

LogicalRect   LogicalFrame();
LogicalRect   LogicalFrame(TextureHandle handle);
LogicalMapping GetLogicalMapping(TextureHandle handle);
float         LogicalWidth();
float         LogicalHeight();
TextureHandle FindNamedRenderTarget(std::string_view name);
TextureHandle CreateRenderTarget(uint32_t logical_width, uint32_t logical_height, bool has_alpha, bool share_main_depth, bool frame_target);
void          DestroyTexture(TextureHandle handle);

namespace {

constexpr uint32_t kAxisX = 1;
constexpr uint32_t kAxisY = 2;
using Mat4 = detail::Mat4;
constexpr Mat4     kIdentity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

struct Known {
    TextureInfo info;
    std::string name;
};

struct State {
    RendererConfig                          config;
    SDL_Window                             *window = nullptr;
    FrameLayout                             layout;
    float                                   render_scale = 1.0f;
    uint32_t                                main_width = 1;
    uint32_t                                main_height = 1;
    TextureHandle                           target = kMainTarget;
    TextureHandle                           next_texture = kPreviousFrame + 1;
    MeshHandle                              next_mesh = 1;
    std::unordered_map<TextureHandle, Known> textures;
    std::map<std::string, TextureHandle, std::less<>> named;
    UiAnchor                                ui_anchor;
    InterpKey                               interp_key = 0;
    bool                                    no_interpolation = false;
    bool                                    in_frame = false;
    int                                     recording = 0; // the FPS counter records inside a tick
};

// Game code's static constructors call in here before main, in whatever order the platform's linker gives them, so this can't be a global with a constructor of
// its own: on Windows they reached it unbuilt. Never destroyed, for the same reason at exit.
State &G() {
    static State *const state = new State;
    return *state;
}

bool Old() { return dc::render::KazarovDraws(); }

const Known *Find(TextureHandle handle) {
    const auto found = G().textures.find(handle);
    return found == G().textures.end() ? nullptr : &found->second;
}


LogicalMapping MainMapping(uint32_t width, uint32_t height) {
    float w = static_cast<float>(width);
    float h = static_cast<float>(height);
    const float logical_w = G().config.logical_width;
    const float logical_h = G().config.logical_height;
    const float aspect = G().config.display_aspect;
    float       scale_x;
    float       scale_y;
    if (logical_w / logical_h == aspect) {
        // Square texels keep this expression: the general form below rounds differently.
        scale_x = std::min(w / logical_w, h / logical_h);
        scale_y = scale_x;
    } else {
        float shown_w = std::min(w, h * aspect);
        scale_x = shown_w / logical_w;
        scale_y = shown_w / aspect / logical_h;
    }
    return LogicalMapping{scale_x, scale_y, std::floor((w - logical_w * scale_x) * 0.5f), std::floor((h - logical_h * scale_y) * 0.5f), width, height};
}

LogicalMapping UiMapping(const LogicalMapping &mapping) {
    float scale = G().layout.ui_scale;
    if (scale == 1.0f) {
        return mapping;
    }
    float logical_w = G().config.logical_width;
    float logical_h = G().config.logical_height;
    float centre_x = logical_w * 0.5f * mapping.scale_x + mapping.offset_x;
    float centre_y = logical_h * 0.5f * mapping.scale_y + mapping.offset_y;
    float scale_x = mapping.scale_x * scale;
    float scale_y = mapping.scale_y * scale;
    return LogicalMapping{scale_x, scale_y, std::round(centre_x - logical_w * 0.5f * scale_x), std::round(centre_y - logical_h * 0.5f * scale_y), mapping.pixel_width, mapping.pixel_height};
}

bool FillsWindow() { return G().layout.aspect == AspectMode::Fill; }

bool FrameShaped(const TextureInfo &info) { return info.frame_target && FillsWindow(); }

bool FullHeightFrame(const TextureInfo &info) { return info.frame_target && static_cast<float>(info.height) == G().config.logical_height; }

uint32_t ScaledSize(uint32_t logical, float scale) {
    return std::max(1u, static_cast<uint32_t>(std::ceil(static_cast<float>(logical) * scale)));
}

// What a render target's image measures: see Kazarov's TargetPixelSize.
void TargetPixelSize(TextureInfo &info) {
    if (info.shares_main_depth || (FrameShaped(info) && FullHeightFrame(info))) {
        info.pixel_width = G().main_width;
        info.pixel_height = G().main_height;
    } else if (FrameShaped(info)) {
        info.pixel_width = G().main_width;
        info.pixel_height = ScaledSize(info.height, MainMapping(G().main_width, G().main_height).scale_y);
    } else {
        info.pixel_width = ScaledSize(info.width, G().render_scale);
        info.pixel_height = ScaledSize(info.height, G().render_scale);
    }
}

// A resized window or a change of aspect reshapes the targets that follow the frame, in Noktus and so here.
void ReshapeTargets() {
    for (auto &[handle, known] : G().textures) {
        if (known.info.render_target) {
            TargetPixelSize(known.info);
        }
    }
}

void ReadWindowSize() {
    if (G().window == nullptr) {
        return;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(G().window, &width, &height);
    const uint32_t main_width = static_cast<uint32_t>(std::max(width, 1));
    const uint32_t main_height = static_cast<uint32_t>(std::max(height, 1));
    if (main_width != G().main_width || main_height != G().main_height) {
        G().main_width = main_width;
        G().main_height = main_height;
        ReshapeTargets();
    }
}

LogicalMapping TextureMapping(const TextureInfo &info) {
    if (info.shares_main_depth) {
        return MainMapping(info.pixel_width, info.pixel_height);
    }
    if (FrameShaped(info) && FullHeightFrame(info)) {
        LogicalMapping mapping = MainMapping(info.pixel_width, info.pixel_height);
        mapping.scale_x = mapping.scale_x * G().config.logical_width / static_cast<float>(info.width);
        return mapping;
    }
    if (FrameShaped(info)) {
        LogicalMapping main = MainMapping(G().main_width, G().main_height);
        float          rows = static_cast<float>(info.pixel_height) / static_cast<float>(info.height);
        return LogicalMapping{main.scale_x * G().config.logical_width / static_cast<float>(info.width), rows, main.offset_x, 0.0f, info.pixel_width, info.pixel_height};
    }
    return LogicalMapping{static_cast<float>(info.pixel_width) / static_cast<float>(info.width), static_cast<float>(info.pixel_height) / static_cast<float>(info.height), 0.0f, 0.0f, info.pixel_width, info.pixel_height};
}

LogicalRect TargetBounds(const LogicalMapping &mapping) {
    float left = -mapping.offset_x / mapping.scale_x;
    float top = -mapping.offset_y / mapping.scale_y;
    return {left, top, static_cast<float>(mapping.pixel_width) / mapping.scale_x, static_cast<float>(mapping.pixel_height) / mapping.scale_y};
}

uint32_t ExtentAxes(TextureHandle handle) {
    if (!FillsWindow()) {
        return 0;
    }
    if (handle == kMainTarget || handle == kPreviousFrame) {
        return kAxisX | kAxisY;
    }
    const Known *known = Find(handle);
    if (known == nullptr || !known->info.render_target) {
        return 0;
    }
    if (known->info.shares_main_depth) {
        return kAxisX | kAxisY;
    }
    if (!known->info.frame_target) {
        return 0;
    }
    return FullHeightFrame(known->info) ? kAxisX | kAxisY : kAxisX;
}

bool ReachesLow(float low, float high, float edge) { return low <= edge && high > edge; }

bool ReachesHigh(float low, float high, float edge) { return high >= edge && low < edge; }

LogicalRect ExtendRect(LogicalRect rect, uint32_t axes, const LogicalRect &frame, const LogicalMapping &mapping) {
    LogicalRect bounds = TargetBounds(mapping);
    float       left = rect.x;
    float       right = rect.x + rect.w;
    float       top = rect.y;
    float       bottom = rect.y + rect.h;
    if (axes & kAxisX) {
        float low = left;
        float high = right;
        left = ReachesLow(low, high, frame.x) ? std::min(left, bounds.x) : left;
        right = ReachesHigh(low, high, frame.x + frame.w) ? std::max(right, bounds.x + bounds.w) : right;
    }
    if (axes & kAxisY) {
        float low = top;
        float high = bottom;
        top = ReachesLow(low, high, frame.y) ? std::min(top, bounds.y) : top;
        bottom = ReachesHigh(low, high, frame.y + frame.h) ? std::max(bottom, bounds.y + bounds.h) : bottom;
    }
    return {left, top, right - left, bottom - top};
}

uint32_t Crc32(const uint8_t *data, size_t size, uint32_t crc = 0) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> entries{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t value = i;
            for (int bit = 0; bit < 8; bit++) {
                value = (value & 1) ? 0xEDB88320u ^ (value >> 1) : value >> 1;
            }
            entries[i] = value;
        }
        return entries;
    }();
    crc = ~crc;
    for (size_t i = 0; i < size; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

void PutBigEndian(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void PutChunk(std::vector<uint8_t> &out, const char type[4], const std::vector<uint8_t> &data) {
    PutBigEndian(out, static_cast<uint32_t>(data.size()));
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    PutBigEndian(out, Crc32(out.data() + start, out.size() - start));
}

struct Vec3 {
    float x;
    float y;
    float z;
};

Vec3 Column(const Mat4 &m, int c) { return {m[c * 4 + 0], m[c * 4 + 1], m[c * 4 + 2]}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 Scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
bool IsAffine(const Mat4 &m) { return m[3] == 0.0f && m[7] == 0.0f && m[11] == 0.0f && m[15] == 1.0f; }

bool InvertAffine(const Mat4 &m, Mat4 &out) {
    if (!IsAffine(m)) {
        return false;
    }
    Vec3  c0 = Column(m, 0);
    Vec3  c1 = Column(m, 1);
    Vec3  c2 = Column(m, 2);
    Vec3  r0 = Cross(c1, c2);
    Vec3  r1 = Cross(c2, c0);
    Vec3  r2 = Cross(c0, c1);
    float determinant = Dot(c0, r0);
    if (!(std::fabs(determinant) > 1e-12f)) {
        return false;
    }
    float inverse = 1.0f / determinant;
    r0 = Scale(r0, inverse);
    r1 = Scale(r1, inverse);
    r2 = Scale(r2, inverse);
    // The rows of the inverse are r0, r1, r2.
    out = kIdentity;
    Vec3 rows[3] = {r0, r1, r2};
    Vec3 t = Column(m, 3);
    for (int r = 0; r < 3; r++) {
        out[0 * 4 + r] = rows[r].x;
        out[1 * 4 + r] = rows[r].y;
        out[2 * 4 + r] = rows[r].z;
        out[3 * 4 + r] = -Dot(rows[r], t);
    }
    return true;
}

TextureHandle MakeHandle(TextureInfo info, std::string name = {}) {
    const TextureHandle handle = G().next_texture++;
    G().textures[handle] = Known{info, std::move(name)};
    return handle;
}

} // namespace

void RendererInit(SDL_Window *window, const RendererConfig &config) {
    if (Old()) {
        KazarovRendererInit(window, config);
        return;
    }
    G().config = config;
    G().window = window;
    G().layout = config.layout;
    ReadWindowSize();
    G().render_scale = config.render_scale > 0.0f ? config.render_scale : std::max(1.0f, std::round(static_cast<float>(G().main_height) / config.logical_height));
}

void RendererShutdown() {
    if (Old()) {
        KazarovRendererShutdown();
        return;
    }
    G().window = nullptr;
    G().textures.clear();
    G().named.clear();
    G().target = kMainTarget;
    G().in_frame = false;
    G().recording = 0;
}

void RendererResize() {
    if (Old()) {
        KazarovRendererResize();
        return;
    }
    ReadWindowSize();
}

bool HeadlessSurfaceAvailable() { return Old() && KazarovHeadlessSurfaceAvailable(); } // Noktus reads pictures back with no surface at all

uint32_t PipelineCount() { return Old() ? KazarovPipelineCount() : 1; }
float RenderScale() { return Old() ? KazarovRenderScale() : G().render_scale; }

void SetFrameLayout(const FrameLayout &layout) {
    if (Old()) {
        KazarovSetFrameLayout(layout);
        return;
    }
    G().layout = layout;
    ReshapeTargets();
}
FrameLayout CurrentFrameLayout() { return Old() ? KazarovCurrentFrameLayout() : G().layout; }

float LogicalWidth() { return Old() ? KazarovLogicalWidth() : G().config.logical_width; }
float LogicalHeight() { return Old() ? KazarovLogicalHeight() : G().config.logical_height; }
LogicalRect LogicalFrame() { return Old() ? KazarovLogicalFrame() : LogicalRect{0.0f, 0.0f, LogicalWidth(), LogicalHeight()}; }

LogicalRect LogicalFrame(TextureHandle handle) {
    const Known *known = handle == kMainTarget ? nullptr : Find(handle);
    return known == nullptr ? LogicalFrame() : LogicalRect{0.0f, 0.0f, static_cast<float>(known->info.width), static_cast<float>(known->info.height)};
}

LogicalMapping GetLogicalMapping(TextureHandle handle) {
    if (Old()) {
        return KazarovGetLogicalMapping(handle);
    }
    if (handle == kMainTarget || handle == kPreviousFrame) {
        ReadWindowSize();
        return MainMapping(G().main_width, G().main_height);
    }
    ReadWindowSize();
    const Known *known = Find(handle);
    return known == nullptr ? LogicalMapping{1.0f, 1.0f, 0.0f, 0.0f, 0, 0} : TextureMapping(known->info);
}

LogicalMapping GetUiMapping(TextureHandle handle) {
    if (Old()) {
        return KazarovGetUiMapping(handle);
    }
    LogicalMapping mapping = GetLogicalMapping(handle);
    return handle == kMainTarget ? UiMapping(mapping) : mapping;
}

LogicalRect VisibleLogicalRect(TextureHandle handle) {
    if (Old()) {
        return KazarovVisibleLogicalRect(handle);
    }
    LogicalRect frame = LogicalFrame(handle);
    uint32_t    axes = ExtentAxes(handle);
    return axes == 0 ? frame : ExtendRect(frame, axes, frame, GetLogicalMapping(handle));
}

void SetUiAnchor(const UiAnchor &anchor) {
    if (Old()) {
        KazarovSetUiAnchor(anchor);
        return;
    }
    G().ui_anchor = anchor;
}
UiAnchor CurrentUiAnchor() { return Old() ? KazarovCurrentUiAnchor() : G().ui_anchor; }

void SetRenderTarget(TextureHandle target) {
    if (Old()) {
        KazarovSetRenderTarget(target);
        return;
    }
    G().target = target;
}
TextureHandle CurrentRenderTarget() { return Old() ? KazarovCurrentRenderTarget() : G().target; }
bool CanCopyMainDepth(TextureHandle target) {
    if (Old()) {
        return KazarovCanCopyMainDepth(target);
    }
    const Known *known = Find(target);
    return known != nullptr && known->info.render_target && !known->info.shares_main_depth;
}
bool CopyMainDepthToTarget(const LogicalRect &source) { return !Old() || KazarovCopyMainDepthToTarget(source); }
bool DepthPlanesAvailable() { return Old() && KazarovDepthPlanesAvailable(); }

bool BeginFrame() {
    if (Old()) {
        return KazarovBeginFrame();
    }
    G().in_frame = true;
    return true;
}
void EndFrame() {
    if (Old()) {
        KazarovEndFrame();
        return;
    }
    G().in_frame = false;
}
bool InFrame() { return Old() ? KazarovInFrame() : G().in_frame || G().recording > 0; }

void BeginRecording(RecordingKind kind) {
    if (Old()) {
        KazarovBeginRecording(kind);
        return;
    }
    G().recording++;
}
DisplayListRef EndRecording() {
    if (Old()) {
        return KazarovEndRecording();
    }
    G().recording--;
    return nullptr;
}
bool Recording() { return Old() ? KazarovRecording() : G().recording > 0; }

void SetInterpKey(InterpKey key, bool no_interpolation, float teleport_distance, bool blend_vertices) {
    if (Old()) {
        KazarovSetInterpKey(key, no_interpolation, teleport_distance, blend_vertices);
        return;
    }
    G().interp_key = key;
    G().no_interpolation = no_interpolation;
}
InterpKey CurrentInterpKey() { return Old() ? KazarovCurrentInterpKey() : G().interp_key; }
bool CurrentNoInterpolation() { return Old() ? KazarovCurrentNoInterpolation() : G().no_interpolation; }
void CutInterpolation() {
    if (Old()) {
        KazarovCutInterpolation();
    }
}
void CutCameraInterpolation() {
    if (Old()) {
        KazarovCutCameraInterpolation();
    }
}

// A size no GPU the game supports can hold: Noktus would stop the game over it, where the game can do without the picture.
constexpr uint32_t kLargestTexture = 16384;

TextureHandle CreateTexture(const TextureDesc &desc) {
    if (Old()) {
        return KazarovCreateTexture(desc);
    }
    const uint32_t longest = std::max(desc.width, desc.height);
    if (longest == 0 || desc.width == 0 || desc.height == 0 || longest > kLargestTexture || desc.mip_levels == 0 || desc.mip_levels > static_cast<uint32_t>(std::bit_width(longest))) {
        std::fprintf(stderr, "texture: %ux%u with %u levels can't be made\n", desc.width, desc.height, desc.mip_levels);
        return kNullTexture;
    }
    TextureInfo info = {};
    info.width = info.pixel_width = desc.width;
    info.height = info.pixel_height = desc.height;
    info.format = desc.format;
    info.mip_levels = desc.mip_levels;
    info.has_alpha = desc.has_alpha;
    return MakeHandle(info);
}

TextureHandle CreatePalette() {
    if (Old()) {
        return KazarovCreatePalette();
    }
    TextureInfo info = {};
    info.width = info.pixel_width = 256;
    info.height = info.pixel_height = 1;
    info.format = TextureFormat::Rgba8;
    info.mip_levels = 1;
    info.has_alpha = true;
    return MakeHandle(info);
}

TextureHandle CreateRenderTarget(uint32_t logical_width, uint32_t logical_height, bool has_alpha, bool share_main_depth, bool frame_target) {
    if (Old()) {
        return KazarovCreateRenderTarget(logical_width, logical_height, has_alpha, share_main_depth, frame_target);
    }
    if (share_main_depth) {
        logical_width = static_cast<uint32_t>(G().config.logical_width);
        logical_height = static_cast<uint32_t>(G().config.logical_height);
        frame_target = false;
    }
    if (logical_width == 0 || logical_height == 0) {
        return kNullTexture;
    }
    TextureInfo info = {};
    info.width = logical_width;
    info.height = logical_height;
    info.format = TextureFormat::Rgba8;
    info.mip_levels = 1;
    info.has_alpha = has_alpha;
    info.render_target = true;
    info.shares_main_depth = share_main_depth;
    info.frame_target = frame_target;
    TargetPixelSize(info);
    return MakeHandle(info);
}

TextureHandle FindNamedRenderTarget(std::string_view name) {
    if (Old()) {
        return KazarovFindNamedRenderTarget(name);
    }
    const auto found = G().named.find(name);
    return found == G().named.end() ? kNullTexture : found->second;
}

TextureHandle NamedRenderTarget(std::string_view name, uint32_t logical_width, uint32_t logical_height, bool has_alpha, bool share_main_depth, bool frame_target) {
    if (Old()) {
        return KazarovNamedRenderTarget(name, logical_width, logical_height, has_alpha, share_main_depth, frame_target);
    }
    const TextureHandle before = FindNamedRenderTarget(name);
    if (const Known *known = Find(before)) {
        const TextureInfo &info = known->info;
        if ((share_main_depth && info.shares_main_depth) ||
            (info.width == logical_width && info.height == logical_height && info.has_alpha == has_alpha && info.shares_main_depth == share_main_depth && info.frame_target == frame_target)) {
            return before;
        }
        DestroyTexture(before);
    }
    const TextureHandle target = CreateRenderTarget(logical_width, logical_height, has_alpha, share_main_depth, frame_target);
    if (target != kNullTexture) {
        G().named.insert_or_assign(std::string(name), target);
    }
    return target;
}

std::optional<TextureInfo> GetTextureInfo(TextureHandle handle) {
    if (Old()) {
        return KazarovGetTextureInfo(handle);
    }
    if (handle == kMainTarget || handle == kPreviousFrame) {
        ReadWindowSize();
        return TextureInfo{static_cast<uint32_t>(G().config.logical_width), static_cast<uint32_t>(G().config.logical_height), G().main_width, G().main_height, TextureFormat::Rgba8, 1, true, true, false, false};
    }
    ReadWindowSize();
    const Known *known = Find(handle);
    return known == nullptr ? std::nullopt : std::optional<TextureInfo>(known->info);
}

void DestroyTexture(TextureHandle handle) {
    if (Old()) {
        KazarovDestroyTexture(handle);
        return;
    }
    std::erase_if(G().named, [&](const auto &entry) { return entry.second == handle; });
    G().textures.erase(handle);
}

bool UpdateTexture(TextureHandle texture, uint32_t mip, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void *pixels, uint32_t row_pixels) {
    return Old() ? KazarovUpdateTexture(texture, mip, x, y, width, height, pixels, row_pixels) : Find(texture) != nullptr;
}
bool UpdateTextureNow(TextureHandle texture, uint32_t mip, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void *pixels, uint32_t row_pixels) {
    return Old() ? KazarovUpdateTextureNow(texture, mip, x, y, width, height, pixels, row_pixels) : Find(texture) != nullptr;
}
bool UpdatePalette(TextureHandle palette, const uint32_t *colors, uint32_t first, uint32_t count) {
    return Old() ? KazarovUpdatePalette(palette, colors, first, count) : Find(palette) != nullptr;
}

MeshHandle CreateMesh(std::span<const Vertex3D> vertices, std::span<const uint32_t> indices) {
    if (Old()) {
        return KazarovCreateMesh(vertices, indices);
    }
    return vertices.empty() || indices.empty() ? kNullMesh : G().next_mesh++;
}
bool UpdateMeshVertices(MeshHandle mesh, uint32_t first, std::span<const Vertex3D> vertices) { return !Old() || KazarovUpdateMeshVertices(mesh, first, vertices); }
void DestroyMesh(MeshHandle mesh) {
    if (Old()) {
        KazarovDestroyMesh(mesh);
    }
}

void ConvertPs2Alpha(uint32_t *rgba, size_t count) {
    uint8_t *bytes = reinterpret_cast<uint8_t *>(rgba);
    for (size_t i = 0; i < count; i++) {
        uint8_t &alpha = bytes[i * 4 + 3];
        alpha = static_cast<uint8_t>(std::min(alpha * 2, 255));
    }
}

MeshTransform IdentityMeshTransform() {
    MeshTransform transform;
    for (float *m : {transform.projection, transform.view, transform.middle, transform.model, transform.local}) {
        std::memcpy(m, kIdentity.data(), sizeof(float) * 16);
    }
    return transform;
}

bool InvertAffineTransform(const float matrix[16], float inverse[16]) {
    Mat4 in;
    Mat4 out;
    std::memcpy(in.data(), matrix, sizeof(float) * 16);
    if (!InvertAffine(in, out)) {
        return false;
    }
    std::memcpy(inverse, out.data(), sizeof(float) * 16);
    return true;
}

// Kazarov's overlay text, which is the FPS counter and the text-wipe tool's labels, draws through this.
void Draw2D(Primitive primitive, std::span<const Vertex2D> vertices, const TextureBinding &binding, const DrawState &state, const Presentation2D *presentation) {
    if (Old()) {
        KazarovDraw2D(primitive, vertices, binding, state, presentation);
        return;
    }
    dc::render::CountDraw();
    dc::render::MirrorDraw2D(primitive, vertices, binding, state, true);
}

bool WritePng(const std::filesystem::path &path, const uint8_t *rgba, uint32_t width, uint32_t height) {
    std::vector<uint8_t> raw;
    raw.reserve((static_cast<size_t>(width) * 3 + 1) * height);
    for (uint32_t y = 0; y < height; y++) {
        raw.push_back(0);
        const uint8_t *row = rgba + static_cast<size_t>(y) * width * 4;
        for (uint32_t x = 0; x < width; x++) {
            raw.insert(raw.end(), row + x * 4, row + x * 4 + 3);
        }
    }

    // zlib stream of stored deflate blocks: no compressor, and screenshots are for tests.
    std::vector<uint8_t> zlib = {0x78, 0x01};
    size_t               pos = 0;
    do {
        size_t length = std::min<size_t>(raw.size() - pos, 0xFFFF);
        bool   last = pos + length == raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<uint8_t>(length));
        zlib.push_back(static_cast<uint8_t>(length >> 8));
        zlib.push_back(static_cast<uint8_t>(~length));
        zlib.push_back(static_cast<uint8_t>(~length >> 8));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                    raw.begin() + static_cast<std::ptrdiff_t>(pos + length));
        pos += length;
    } while (pos < raw.size());
    uint32_t a = 1;
    uint32_t b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    PutBigEndian(zlib, (b << 16) | a);

    std::vector<uint8_t> header;
    PutBigEndian(header, width);
    PutBigEndian(header, height);
    header.insert(header.end(), {8, 2, 0, 0, 0});

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    PutChunk(png, "IHDR", header);
    PutChunk(png, "IDAT", zlib);
    PutChunk(png, "IEND", {});

    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    return file &&
           file.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
}

} // namespace gfx
