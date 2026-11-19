#include "firelight.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>
#include <unordered_map>
#include <utility>

#include "frame.hpp"
#include "objanime.hpp"
#include "render/renderer_calls.hpp"

namespace dc::game {

namespace {

// The game gave a character a town fire's light in full within 5 units, easing to none at 40, at a brightness drawn from 80 to 127 every tick (128 the
// art's own), 104 on average. A town's torches burn 35 to 55 units above the ground, so a character under one was barely lit, and real falloff matched
// to that left Toan unlit beneath them (the owner's playtest, 2026-11-14): a fire's light is its average brightness at 15 units, reaching to 80, which
// lights a character under a torch and the wall beside it as a flame would. DC_NOKTUS_FIRE scales it, for tuning by eye, 0 for none.
constexpr float kBrightness = 104.0f / 128.0f;
constexpr float kMatchDistance = 15.0f;
constexpr float kRange = 80.0f;
constexpr float kSize = 3.0f; // the flame's own size: nearer, no brighter
// A town's fire (and the opening's) lights nothing brighter than it lit at the distance it was matched at, the game's full light for a character: nearer, at the flame's
// size, a lamp by a door lit the wall and Toan's poncho beside it many times past white, a flat blown-out orange (the owner's report, 2026-11-16).
// Further off, as before.
constexpr float kTownSize = kMatchDistance;
// A fire's light is warm, as a torch's is, about 2700 K, at the luminance the game's grey light had: where the game lit a character grey, a
// fire's light now colours what it lights as its flame looks.
constexpr float kWarm[3] = {1.987f, 0.788f, 0.189f};

// A scale from the environment, for tuning by eye: the default unless it's set to a number, never below 0.
float ScaleFrom(const char *name, float otherwise = 1.0f) {
    const char *asked = std::getenv(name);
    if (asked == nullptr) {
        return otherwise;
    }
    char       *end = nullptr;
    const float wanted = std::strtof(asked, &end);
    return end != asked && *end == '\0' && std::isfinite(wanted) ? std::max(wanted, 0.0f) : otherwise;
}

float FireScale() {
    static const float scale = ScaleFrom("DC_NOKTUS_FIRE");
    return scale;
}

// DC_NOKTUS_GLOW scales the halos' glows alone, on top of DC_NOKTUS_FIRE.
float GlowScale() {
    static const float scale = ScaleFrom("DC_NOKTUS_GLOW");
    return scale;
}

// Flickering as a fire does, gently: the light a share brighter or dimmer, a new share every few ticks, eased towards it each tick. The game drew a new
// brightness every tick, which with light added as light reads as a strobe (the owner's report, 2026-11-13), and on the walls a fire lights, as too
// quick even eased (2026-11-14). From numbers of its own, so the game's random numbers stay in step.
constexpr float kFlickerShare = 0.06f;
constexpr int   kFlickerTicks = 8;
constexpr float kFlickerEase = 0.1f;

struct Flicker {
    float share = 1.0f;
    float towards = 1.0f;
    int   ticks = 0;
};

float Flickering(std::uint64_t key) {
    static std::minstd_rand                            numbers;
    static std::uniform_real_distribution<float>       drawn(1.0f - kFlickerShare, 1.0f + kFlickerShare);
    static std::unordered_map<std::uint64_t, Flicker> fires;
    Flicker &fire = fires[key];
    if (fire.ticks == 0) {
        fire.towards = drawn(numbers);
    }
    fire.ticks = (fire.ticks + 1) % kFlickerTicks;
    fire.share += (fire.towards - fire.share) * kFlickerEase;
    return fire.share;
}

// A dungeon's torches hang on its walls a few units from what they light, where a town's burn 35 to 55 above its streets, so a dungeon's fire gives
// a share of a town's light: the owner found the Divine Beast Cave about a tenth too bright (2026-11-15), and this share takes a tenth off its
// picture's light there (measured on the cave's opening: 0.0985 of linear light on average, 0.0887 with it).
constexpr float kDungeonShare = 0.45f;

// A candle's flame is a tenth of a fire's as the game draws it: its light a share of a torch's, reaching less far, from a smaller flame.
constexpr float kCandleShare = 0.15f;
constexpr float kCandleRange = 40.0f;
constexpr float kCandleSize = 1.0f;

float LinearOf(float encoded) {
    const float c = std::max(encoded, 0.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

void GiveFlameLight(std::uint64_t key, const float *flame, float share, float range, float size) {
    if (!render::ModernLight() || FireScale() == 0.0f) {
        return;
    }
    const float light = LinearOf(kBrightness) * kMatchDistance * kMatchDistance * FireScale() * share * Flickering(key);
    render::AddPointLight({
        .key = key,
        .position = {flame[0],         flame[1],         flame[2]        },
        .color = {light * kWarm[0], light * kWarm[1], light * kWarm[2]},
        .range = range,
        .size = size
    });
}

// A halo's glow (HaloAsGlow): a little wider than the flame the game draws inside its halo (a fifth of the halo's width each side of its middle), its
// middle past white at the light's warmth, so bloom spreads a glow from the flame itself, the light round about being the fire's own. Its light falls
// away from its heart as glare does (glow.frag), so it has no edge: the first shape, light held over most of its width, read as a pale ball round the
// lamp rather than its light (the owner's look, 2026-11-18). At its width and light before (0.2, 1.5) the new shape was barely a glow; at twice its
// width a haze of orange over the lamp.
constexpr float kGlowShare = 0.26f;
constexpr float kGlowLight = 2.25f;
// The glow's colour: the fire's light paled this share of the way to white of its brightness. At the light's own warmth the first shape read as a deep
// orange disc, too saturated for the bright heart of a flame (the owner's look, 2026-11-16), so it was paled 0.4; the glare's heart, past white,
// eases towards white in the tone map anyway, so it's warmer now, its fall the fire's colour (2026-11-18). DC_NOKTUS_GLOW_PALE sets the share, 0 to 1,
// for tuning by eye.
constexpr float kGlowPale = 0.2f;

float GlowPale() {
    static const float pale = std::min(ScaleFrom("DC_NOKTUS_GLOW_PALE", kGlowPale), 1.0f);
    return pale;
}

// A torch's open fire glows yellower than a lamp's paled orange (the owner's look, 2026-11-16, lamps kept as they were): its glow this share of the
// way on to a flame's yellow of the same brightness. DC_NOKTUS_TORCH_YELLOW sets the share, 0 to 1, for tuning by eye.
constexpr float kTorchYellow = 0.35f;
constexpr float kFlameYellow[3] = {1.139f, 1.025f, 0.342f}; // (1, 0.9, 0.3) at a brightness of 1

float TorchYellow() {
    static const float yellow = std::min(ScaleFrom("DC_NOKTUS_TORCH_YELLOW", kTorchYellow), 1.0f);
    return yellow;
}
int             g_halo_as_glow = 0;
bool            g_halo_dungeon = false;
std::uint64_t   g_flame_key = 0;

// The renderer's flame (render::AddFire) over the game's sprite of it (18 units across at scale 1, centred on the flame's middle): its foot in its
// torch's head, below that middle, its tip where the sprite's top was. Its heart's light past white, for
// bloom; DC_NOKTUS_FLAME scales it, for tuning by eye, on top of DC_NOKTUS_FIRE.
constexpr float kFlameFoot = 6.0f;
constexpr float kFlameHeight = 15.0f;
constexpr float kFlameWidth = 10.0f;
constexpr float kFlameLight = 1.4f;
// How much of its light a flame gives off round about from its own shape, for bloom to spread, in place of the halo's glow: the flame itself glowing,
// as a modern game's emitters do, where a glow beside it read as a glowing ball giving the light (the owner, 2026-11-18). DC_NOKTUS_GLOW scales it.
constexpr float kFlameGlow = 4.0f;

float FlameScale() {
    static const float scale = ScaleFrom("DC_NOKTUS_FLAME");
    return scale;
}

} // namespace

HaloAsGlow::HaloAsGlow(bool dungeon) : previous_dungeon_(std::exchange(g_halo_dungeon, dungeon)) {
    g_halo_as_glow++;
}

HaloAsGlow::~HaloAsGlow() {
    g_halo_as_glow--;
    g_halo_dungeon = previous_dungeon_;
}

bool GiveHaloGlow(const float *middle, float scale, float flicker, float seen_through, bool open_flame) {
    if (g_halo_as_glow <= 0 || !render::ModernGlow() || !(scale > 0.0f)) {
        return false;
    }
    // A dungeon's fire glows at the share of a town's its light is (kDungeonShare): its torches hang close to what they light, and at a town's
    // its lanterns were balls of pale light where the game's halos were warm and small (step 11, once the dungeons' fires glow).
    const float light = kGlowLight * FireScale() * GlowScale() * (g_halo_dungeon ? kDungeonShare : 1.0f);
    // White of the warm light's own brightness (its luminance, by Rec. 709's weights), so paling doesn't brighten it.
    const float brightness = 0.2126f * kWarm[0] + 0.7152f * kWarm[1] + 0.0722f * kWarm[2];
    float       colour[3];
    for (int c = 0; c < 3; c++) {
        const float paled = kWarm[c] + (brightness - kWarm[c]) * GlowPale();
        colour[c] = light * (open_flame ? paled + (brightness * kFlameYellow[c] - paled) * TorchYellow() : paled);
    }
    return render::AddGlow({
        .position = {middle[0], middle[1], middle[2]},
        .color = {colour[0], colour[1], colour[2]},
        .size = 45.0f * scale * flicker * kGlowShare,
        .seen_through = seen_through
    });
}

FlameKey::FlameKey(std::uint64_t key) : previous_(std::exchange(g_flame_key, key)) {}

FlameKey::~FlameKey() {
    g_flame_key = previous_;
}

bool HaloGlowWanted() {
    return g_halo_as_glow > 0 && render::ModernGlow();
}

bool GiveFlame(const float *middle, float scale, float fog, bool glows) {
    if (!(scale > 0.0f) || !render::ModernFire()) {
        return false;
    }
    // Who it is: the fire's own (FlameKey), or from where it burns (a dungeon's fires are one object, moved from fire to fire), so each flickers as
    // its own.
    const auto place = [](float at) { return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::lround(at * 4.0f)) & 0x1FFFFF); };
    const std::uint64_t key = g_flame_key != 0 ? g_flame_key : (1ull << 63) | place(middle[0]) << 42 | place(middle[1]) << 21 | place(middle[2]);
    const float         light = kFlameLight * FireScale() * FlameScale();
    // Its glow, from the flame itself, as a modern game's emitters glow: a dungeon's at the share of a town's its light is, as its halo's was.
    const float         glow = glows ? kFlameGlow * GlowScale() * (g_halo_dungeon ? kDungeonShare : 1.0f) : 0.0f;
    return render::AddFire({
        .key = key,
        .position = {middle[0], middle[1] - kFlameFoot * scale, middle[2]},
        .height = kFlameHeight * scale,
        .width = kFlameWidth * scale,
        .light = {light, light, light},
        .fog = fog,
        .seen_through = 0.0f,
        .glow = glow
    });
}

void GiveFireLight(std::uint64_t key, const float *flame) {
    GiveFlameLight(key, flame, 1.0f, kRange, kTownSize);
}

void GiveDungeonFireLight(std::uint64_t key, const float *flame) {
    GiveFlameLight(key, flame, kDungeonShare, kRange, kSize);
}

void GiveCandleLight(std::uint64_t key, const float *flame) {
    GiveFlameLight(key, flame, kCandleShare, kCandleRange, kCandleSize);
}

void GiveOpeningFireLight(int index, const float *pos, int fire_flag) {
    // Keys apart from the town's (effects' places in memory), the dungeons' and the Atlamillia's.
    constexpr std::uint64_t kOpeningFireKeys = 1ull << 59;
    // The burning village moves the fires it puts out to -100 (times 10).
    constexpr float kHidden = -500.0f;
    if (fire_flag != 1 || pos[1] < kHidden) {
        return;
    }
    const float flame[3] = {pos[0], pos[1] + kFlameRise, pos[2]};
    GiveFireLight(kOpeningFireKeys | static_cast<std::uint64_t>(index), flame);
}

void GiveEffectLight(const EDIT_EFFECT_INFO *effect) {
    if (effect == nullptr || effect->frame == nullptr || !render::ModernLight()) {
        return;
    }
    const auto key = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(effect));
    float      flame[4];
    // Where DrawEditEffect draws the flame: a fire's above where it stands, a candle's 4 units lower, unless the candle has no flame.
    switch (effect->kind) {
        case EDIT_EFFECT_FIRE_LARGE:
        case EDIT_EFFECT_FIRE_MEDIUM:
        case EDIT_EFFECT_FIRE_SMALL:
            effect->frame->GetWorldPosition(flame, const_cast<float *>(effect->offset));
            flame[1] += kFlameRise;
            GiveFireLight(key, flame);
            break;
        case EDIT_EFFECT_CANDLE:
            if (effect->sound_no > 0.0f) {
                break;
            }
            effect->frame->GetWorldPosition(flame, const_cast<float *>(effect->offset));
            flame[1] += kFlameRise - 4.0f;
            GiveCandleLight(key, flame);
            break;
        default:
            break;
    }
}

} // namespace dc::game
