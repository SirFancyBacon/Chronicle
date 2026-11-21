#include <gtest/gtest.h>

#include <string>

#include "noktus/sequence.hpp"
#include "sound/published.hpp"

// Sequence documents read for Noktus's sequencer (its audio plan's slice 3).
namespace {

using namespace dc;
using Kind = noktus::audio::SequenceEvent::Kind;

TEST(NoktusSequence, ReadsASequenceDocument) {
    const std::string text = R"({"kind": "sequence", "schema_version": 1, "songs": [{"division": 240, "events": [
        {"delta": 0, "kind": "meta", "meta": 81, "payload": [7, 161, 32]},
        {"delta": 10, "kind": "channel", "status": 176, "data": [7, 100]},
        {"delta": 5, "kind": "meta", "meta": 47, "payload": []},
        {"delta": 7, "kind": "channel", "status": 144, "data": [60, 90]},
        {"delta": 3, "kind": "end"}]}]})";
    const auto        song = sound::NoktusSequenceFromJson(text);
    ASSERT_NE(song, nullptr);
    EXPECT_EQ(song->division, 240);
    ASSERT_EQ(song->events.size(), 4u);
    EXPECT_EQ(song->events[0].kind, Kind::Tempo);
    EXPECT_EQ(song->events[0].tempo, 500000u);
    EXPECT_EQ(song->events[1].kind, Kind::Channel);
    EXPECT_EQ(song->events[1].status, 176);
    EXPECT_EQ(song->events[1].data2, 100);
    // The meta event it passes over gives its time to the note after it.
    EXPECT_EQ(song->events[2].delta, 12u);
    EXPECT_EQ(song->events[2].data1, 60);
    EXPECT_EQ(song->events[3].kind, Kind::End);
    EXPECT_EQ(sound::NoktusSequenceFromJson(text, 1), nullptr);
    EXPECT_EQ(sound::NoktusSequenceFromJson(R"({"kind": "sequence", "songs": [{"events": [{"delta": "x", "kind": "end"}]}]})"), nullptr);
    EXPECT_EQ(sound::NoktusSequenceFromJson(R"({"kind": "bank"})"), nullptr);
    EXPECT_EQ(sound::NoktusSequenceFromJson(R"({"kind": "sequence", "songs": [{"events": [{"delta": -1, "kind": "end"}]}]})"), nullptr);
    // A program change written with its one data byte.
    const auto one = sound::NoktusSequenceFromJson(R"({"kind": "sequence", "songs": [{"events": [{"delta": 0, "kind": "channel", "status": 192, "data": [5]}]}]})");
    ASSERT_NE(one, nullptr);
    ASSERT_EQ(one->events.size(), 1u);
    EXPECT_EQ(one->events[0].data1, 5);
}

} // namespace
