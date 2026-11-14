#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace dc::platform {

/**
 * @file
 * Surround's spread of the stereo mix (audio_output.cpp, Chronicle's audio output in place of Kazarov's).
 */

// Stereo to 5.1 in SDL's channel order: front left and right, centre, bass, back left and right.
// The rears carry the side signal (left minus right) 12 ms late and with the top rolled off, the
// way a matrix decoder places ambience behind the listener; the centre is the sum. The bass channel
// (LFE) stays silent, as a matrix upmix leaves it: the other channels carry their own bass, which a
// receiver's bass management sends to the subwoofer. Kazarov's fed it the whole mix's bass as well,
// played twice and 10 dB up by a receiver, which shook the owner's floor by a torch (2026-11-14).
struct SurroundUpmix {
    static constexpr int kRearChannels = 6;
    std::vector<float>   delay;
    std::size_t          at = 0;
    float                rear = 0.0f;

    void Reset(int rate) {
        delay.assign(std::max<std::size_t>(1, static_cast<std::size_t>(rate * 12 / 1000)), 0.0f);
        at = 0;
        rear = 0.0f;
    }

    void Run(const float *in, float *out, int frames, int rate) {
        const float rear_k = 1.0f - std::exp(-6.2831853f * 7000.0f / static_cast<float>(rate));
        for (int i = 0; i < frames; i++) {
            const float l = in[i * 2];
            const float r = in[i * 2 + 1];
            const float mid = (l + r) * 0.5f;
            const float side = (l - r) * 0.5f;
            const float late = delay[at];
            delay[at] = side;
            at = (at + 1) % delay.size();
            rear += (late - rear) * rear_k;
            float *o = out + i * kRearChannels;
            o[0] = l * 0.85f;
            o[1] = r * 0.85f;
            o[2] = mid * 0.7f;
            o[3] = 0.0f;
            o[4] = rear * 0.9f;
            o[5] = -rear * 0.9f;
        }
    }
};

} // namespace dc::platform
