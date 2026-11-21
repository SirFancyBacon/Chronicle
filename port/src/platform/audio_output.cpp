// Chronicle's audio output, in place of Kazarov's platform/audio.cpp (left out of dc_host in port/CMakeLists.txt): the same, but for surround's
// silent bass channel (SurroundUpmix, audio_output.hpp). Kept here until it's wanted in Kazarov too, the owner's choice (2026-11-14).
#include "platform/audio_output.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "audio/wav.hpp"
#include "kazarov/log.hpp"
#include "platform/audio.hpp"
#include "platform/clock.hpp"
#include "platform/settings.hpp"
#include "sound/backend.hpp"

namespace {

constexpr int kChunkFrames = 1024;

SDL_AudioStream   *g_stream = nullptr;
AudioRenderFn      g_render = nullptr;
void              *g_user = nullptr;
std::vector<float> g_chunk;
int                g_rate = 48000;
bool               g_surround = false;

dc::platform::SurroundUpmix g_upmix;
std::vector<float>          g_wide;

void SDLCALL Feed(void *, SDL_AudioStream *stream, int additional_amount, int) {
    const int channels = g_surround ? 6 : 2;
    int       frames = additional_amount / static_cast<int>(sizeof(float) * channels);
    while (frames > 0) {
        const int count = std::min(frames, kChunkFrames);
        g_render(g_user, g_chunk.data(), count);
        if (g_surround) {
            g_wide.resize(static_cast<std::size_t>(count) * 6);
            g_upmix.Run(g_chunk.data(), g_wide.data(), count, g_rate);
            SDL_PutAudioStreamData(stream, g_wide.data(), count * static_cast<int>(sizeof(float) * 6));
        } else {
            SDL_PutAudioStreamData(stream, g_chunk.data(), count * static_cast<int>(sizeof(float) * 2));
        }
        frames -= count;
    }
}

// <prefix>_AUDIO_WAV=<path>: the mixer is pulled on the game thread as the clock ticks, so N ticks write
// N / tick rate seconds whatever the run's real speed; a headless run records exactly what it
// would have played.
struct WavCapture {
    audio::WavWriter writer;
    int              rate = 0;
    std::int64_t     last_tick = 0;
    double           owed = 0.0;
};

WavCapture g_wav;

void CaptureTicks() {
    const std::int64_t tick = ClockTickCount();
    if (tick > g_wav.last_tick) {
        g_wav.owed += static_cast<double>(tick - g_wav.last_tick) * g_wav.rate / ClockTickRate();
    }
    g_wav.last_tick = tick;
    auto frames = static_cast<int>(g_wav.owed);
    g_wav.owed -= frames;
    while (frames > 0) {
        const int count = std::min(frames, kChunkFrames);
        g_render(g_user, g_chunk.data(), count);
        g_wav.writer.Append(g_chunk.data(), count);
        frames -= count;
    }
}

bool StartCapture(const char *path, int rate) {
    if (!g_wav.writer.Open(path, rate)) {
        kz::Logf(kz::LogLevel::Error, "audio", "cannot write {}", path);
        return false;
    }
    g_wav.rate = rate;
    g_wav.last_tick = ClockTickCount();
    g_wav.owed = 0.0;
    ClockAddPumpHook(CaptureTicks);
    return true;
}

} // namespace

bool AudioOutputStart(int rate, AudioRenderFn render, void *user) {
    if (AudioOutputRunning()) {
        return true;
    }
    g_render = render;
    g_user = user;
    g_rate = rate;
    g_upmix.Reset(rate);
    g_chunk.assign(kChunkFrames * 2, 0.0f);
    if (const char *path = SettingsEnv("AUDIO_WAV"); path != nullptr && *path != '\0') {
        return StartCapture(path, rate);
    }
    if (const char *setting = SettingsEnv("AUDIO"); setting != nullptr && std::strcmp(setting, "off") == 0) {
        return false;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        kz::Logf(kz::LogLevel::Warning, "audio", "no audio subsystem: {}", SDL_GetError());
        return false;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, g_surround ? 6 : 2, rate};
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, nullptr);
    if (g_stream == nullptr) {
        kz::Logf(kz::LogLevel::Warning, "audio", "no playback device: {}", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    SDL_ResumeAudioStreamDevice(g_stream);
    return true;
}

void AudioSetSurround(bool on) {
    if (on == g_surround) {
        return;
    }
    if (g_stream != nullptr) {
        // What the callback writes changes with the stream's source format, so the order matters:
        // the format first, then the flag the callback reads.
        const SDL_AudioSpec spec{SDL_AUDIO_F32, on ? 6 : 2, g_rate};
        SDL_LockAudioStream(g_stream);
        SDL_SetAudioStreamFormat(g_stream, &spec, nullptr);
        SDL_ClearAudioStream(g_stream);
        g_surround = on;
        g_upmix.Reset(g_rate);
        SDL_UnlockAudioStream(g_stream);
        return;
    }
    g_surround = on;
}

void AudioOutputStop() {
    if (g_wav.writer.IsOpen()) {
        ClockRemovePumpHook(CaptureTicks);
        g_wav.writer.Close();
    }
    if (g_stream == nullptr) {
        return;
    }
    SDL_DestroyAudioStream(g_stream);
    g_stream = nullptr;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool AudioOutputRunning() {
    return g_stream != nullptr || g_wav.writer.IsOpen();
}

void AudioOutputPause(bool paused) {
    // Kazarov's window pauses the output when the app goes to the background: Noktus's engine, where it plays.
    if (dc::sound::ActiveEngine() == dc::sound::Engine::Noktus) {
        dc::sound::Audio().PauseOutput(paused);
        return;
    }
    if (g_stream == nullptr) {
        return;
    }
    if (paused) {
        SDL_PauseAudioStreamDevice(g_stream);
    } else {
        SDL_ResumeAudioStreamDevice(g_stream);
    }
}
