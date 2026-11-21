// The game's own sound files read for Noktus (sound/retail.hpp), from bytes made here.
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "sound/retail.hpp"

namespace dc::test {

namespace {

namespace na = noktus::audio;

void Put(std::vector<std::uint8_t> &bytes, std::size_t at, std::uint32_t value, std::size_t size) {
    if (bytes.size() < at + size) {
        bytes.resize(at + size);
    }
    for (std::size_t i = 0; i < size; i++) {
        bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

// A chunk's tag, as the files store it: "SCEI" and its name, each a little-endian word of the four letters read backwards.
std::uint32_t Word(const char (&name)[5]) {
    return static_cast<std::uint32_t>(name[0]) << 24 | static_cast<std::uint32_t>(name[1]) << 16 | static_cast<std::uint32_t>(name[2]) << 8 |
           static_cast<std::uint32_t>(name[3]);
}

} // namespace

TEST(RetailAudio, SequenceConversionPreservesRunningDeltasAndNoteOff) {
    std::vector<std::uint8_t> bytes(0x1A);
    Put(bytes, 0, Word("SCEI"), 4);
    Put(bytes, 4, Word("Midi"), 4);
    Put(bytes, 8, 0x24, 4);    // the chunk's size
    Put(bytes, 0x0C, 0, 4);    // the highest song: one song
    Put(bytes, 0x10, 0x14, 4); // its place
    Put(bytes, 0x14, 6, 4);    // its events, past its header
    Put(bytes, 0x18, 480, 2);  // its ticks a beat
    // A note on whose velocity's high bit says the next event comes at once; Sony's note-off, its note alone; the end.
    bytes.insert(bytes.end(), {0, 0x90, 60, 0xE4, 0x80, 60, 0, 0xFF, 0x2F, 0});
    const auto songs = dc::sound::RetailSequences(bytes);
    ASSERT_EQ(songs.size(), 1u);
    const na::Sequence &song = *songs[0];
    EXPECT_EQ(song.division, 480);
    ASSERT_EQ(song.events.size(), 3u);
    EXPECT_EQ(song.events[0].kind, na::SequenceEvent::Kind::Channel);
    EXPECT_EQ(song.events[0].status, 0x90);
    EXPECT_EQ(song.events[0].data2, 100);
    EXPECT_EQ(song.events[1].status, 0x80);
    EXPECT_EQ(song.events[1].data1, 60);
    EXPECT_EQ(song.events[1].delta, 0u);
    EXPECT_EQ(song.events[2].kind, na::SequenceEvent::Kind::End);
}

TEST(RetailAudio, ASongWithNoPlaceKeepsTheOthersNumbers) {
    std::vector<std::uint8_t> bytes(0x1E);
    Put(bytes, 0, Word("SCEI"), 4);
    Put(bytes, 4, Word("Midi"), 4);
    Put(bytes, 0x0C, 1, 4);           // two songs
    Put(bytes, 0x10, 0xFFFFFFFF, 4);  // the first has no place
    Put(bytes, 0x14, 0x18, 4);        // the second's
    Put(bytes, 0x18, 6, 4);
    Put(bytes, 0x1C, 96, 2);
    // A tempo change (500,000 microseconds a beat), then the end, ten ticks on: the tempo's own delta carried.
    bytes.insert(bytes.end(), {5, 0xFF, 0x51, 3, 0x07, 0xA1, 0x20, 10, 0xFF, 0x2F, 0});
    Put(bytes, 8, static_cast<std::uint32_t>(bytes.size()), 4);
    const auto songs = dc::sound::RetailSequences(bytes);
    ASSERT_EQ(songs.size(), 2u);
    EXPECT_TRUE(songs[0]->events.empty());
    EXPECT_EQ(songs[0]->division, 480);
    const na::Sequence &song = *songs[1];
    EXPECT_EQ(song.division, 96);
    ASSERT_EQ(song.events.size(), 2u);
    EXPECT_EQ(song.events[0].kind, na::SequenceEvent::Kind::Tempo);
    EXPECT_EQ(song.events[0].tempo, 500000u);
    EXPECT_EQ(song.events[0].delta, 5u);
    EXPECT_EQ(song.events[1].kind, na::SequenceEvent::Kind::End);
    EXPECT_EQ(song.events[1].delta, 10u);
}

TEST(RetailAudio, RawSamplesKeepTheSpuLoopBoundary) {
    std::vector<std::uint8_t> bytes(32, 0x77);
    bytes[0] = bytes[16] = 0;
    bytes[1] = 0x04;         // the loop starts here
    bytes[17] = 0x01 | 0x02; // the end, and it repeats
    const dc::sound::RetailWave wave = dc::sound::DecodeRetailWave(bytes);
    EXPECT_EQ(wave.samples.size(), 56u);
    EXPECT_TRUE(wave.loops);
    EXPECT_EQ(wave.loop_start, 0u);
    EXPECT_EQ(wave.samples[0], 28672.0f / 32768.0f);
}

TEST(RetailAudio, ABankReadsItsTablesAndWaveforms) {
    // An HD of the four tables the bank needs, found where the header points: one program of one split, one sample set of one sample, one
    // waveform at the BD's start, 22,050 a second.
    std::vector<std::uint8_t> hd;
    const auto                chunk = [&](std::size_t at, const char (&name)[5], std::uint32_t size) {
        Put(hd, at, Word("SCEI"), 4);
        Put(hd, at + 4, Word(name), 4);
        Put(hd, at + 8, size, 4);
    };
    chunk(0x00, "Head", 0x40);
    Put(hd, 0x14, 0x40, 4);
    Put(hd, 0x18, 0xA0, 4);
    Put(hd, 0x1C, 0xC0, 4);
    Put(hd, 0x20, 0x100, 4);
    chunk(0x40, "Prog", 0x60);
    Put(hd, 0x4C, 0, 4);         // one program
    Put(hd, 0x50, 0x14, 4);      // at 0x14 into the table
    Put(hd, 0x54, 0x24, 4);      // its splits 0x24 into the program
    Put(hd, 0x58, 1, 1);         // one
    Put(hd, 0x59, 0x14, 1);      // of 0x14 bytes
    Put(hd, 0x5A, 100, 1);       // volume
    Put(hd, 0x5B, 64, 1);        // pan
    Put(hd, 0x78, 0, 2);         // the split: sample set 0
    Put(hd, 0x7A, 36, 1);        // keys 36
    Put(hd, 0x7C, 84, 1);        // to 84
    Put(hd, 0x88, 90, 1);        // its volume
    chunk(0xA0, "Sset", 0x20);
    Put(hd, 0xAC, 0, 4);
    Put(hd, 0xB0, 0x14, 4);
    Put(hd, 0xB4, 2, 1);   // curve 2
    Put(hd, 0xB5, 0, 1);   // velocities 0
    Put(hd, 0xB6, 127, 1); // to 127
    Put(hd, 0xB7, 1, 1);   // one sample
    Put(hd, 0xB8, 0, 2);   // sample 0
    chunk(0xC0, "Smpl", 0x40);
    Put(hd, 0xCC, 0, 4);
    Put(hd, 0xD0, 0x14, 4);
    Put(hd, 0xD4, 0, 2);      // waveform 0
    Put(hd, 0xD6, 0, 1);      // velocities 0
    Put(hd, 0xD8, 127, 1);    // to 127
    Put(hd, 0xDF, 60, 1);     // base note
    Put(hd, 0xE4, 120, 1);    // volume
    Put(hd, 0xE6, 0x80FF, 2); // ADSR
    Put(hd, 0xE8, 0x5FC0, 2);
    Put(hd, 0xFD, 0x0F, 1);   // where it goes
    chunk(0x100, "Vagi", 0x20);
    Put(hd, 0x10C, 0, 4);
    Put(hd, 0x110, 0x14, 4);
    Put(hd, 0x114, 0, 4);     // at the BD's start
    Put(hd, 0x118, 22050, 2); // its rate
    hd.resize(0x120);
    std::vector<std::uint8_t> bd(16, 0);
    bd[1] = 0x01; // one block, the end

    const auto bank = dc::sound::RetailBank(hd, bd);
    ASSERT_NE(bank, nullptr);
    ASSERT_EQ(bank->programs.size(), 1u);
    EXPECT_TRUE(bank->programs[0].present);
    EXPECT_EQ(bank->programs[0].volume, 100);
    ASSERT_EQ(bank->programs[0].splits.size(), 1u);
    EXPECT_EQ(bank->programs[0].splits[0].key_low, 36);
    EXPECT_EQ(bank->programs[0].splits[0].key_high, 84);
    EXPECT_EQ(bank->programs[0].splits[0].volume, 90);
    ASSERT_EQ(bank->sample_sets.size(), 1u);
    EXPECT_EQ(bank->sample_sets[0].velocity_curve, 2);
    EXPECT_EQ(bank->sample_sets[0].samples, std::vector<int>{0});
    ASSERT_EQ(bank->samples.size(), 1u);
    EXPECT_EQ(bank->samples[0].wave, 0);
    EXPECT_EQ(bank->samples[0].base_note, 60);
    EXPECT_EQ(bank->samples[0].volume, 120);
    ASSERT_EQ(bank->waves.size(), 1u);
    ASSERT_NE(bank->waves[0].samples, nullptr);
    EXPECT_EQ(bank->waves[0].samples->rate, 22050);
    EXPECT_EQ(bank->waves[0].samples->data.size(), 28u);
    EXPECT_FALSE(bank->waves[0].loops);

    // A BD too short for its waveform: the bank, with that waveform silent.
    const auto short_bd = dc::sound::RetailBank(hd, std::vector<std::uint8_t>{});
    ASSERT_NE(short_bd, nullptr);
    EXPECT_EQ(short_bd->waves[0].samples, nullptr);
    // An HD without its tables: none.
    EXPECT_EQ(dc::sound::RetailBank(std::vector<std::uint8_t>(64, 0), bd), nullptr);
}

} // namespace dc::test
