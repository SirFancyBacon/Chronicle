#pragma once

namespace dc::platform {

// DC_SPEAKER_TEST=stereo|5.1|7.1: Noktus's audio heard on the real speakers before the game uses it. Pink noise from each speaker in turn, round
// the room clockwise from the front left as a receiver's test tones go, then a sound circling the room twice, then music's left and right spread all
// around. Printed as it goes.
enum class SpeakerTest {
    Done,
    BadLayout,
    NoDevice
};
SpeakerTest RunSpeakerTest(const char *layout);

} // namespace dc::platform
