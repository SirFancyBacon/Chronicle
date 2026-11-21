#include "sound/retail.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace dc::sound {

namespace na = noktus::audio;

namespace {

// Sony's sound files are chunks, each led by "SCEI" and its own name, in either byte order.
constexpr std::uint32_t kNone = 0xFFFFFFFF;

constexpr std::uint32_t Tag(const char (&name)[5]) {
    return static_cast<std::uint32_t>(name[0]) << 24 | static_cast<std::uint32_t>(name[1]) << 16 | static_cast<std::uint32_t>(name[2]) << 8 |
           static_cast<std::uint32_t>(name[3]);
}

class Bytes {
public:
    explicit Bytes(std::span<const std::uint8_t> data) : data_(data) {}

    std::size_t size() const { return data_.size(); }

    std::uint8_t U8(std::size_t at) const { return at < data_.size() ? data_[at] : 0; }
    int          S8(std::size_t at) const { return static_cast<std::int8_t>(U8(at)); }

    std::uint16_t U16(std::size_t at) const {
        if (at > data_.size() || data_.size() - at < 2) {
            return 0;
        }
        return static_cast<std::uint16_t>(data_[at] | data_[at + 1] << 8);
    }

    std::uint32_t U32(std::size_t at) const {
        if (at > data_.size() || data_.size() - at < 4) {
            return kNone;
        }
        return static_cast<std::uint32_t>(data_[at]) | static_cast<std::uint32_t>(data_[at + 1]) << 8 |
               static_cast<std::uint32_t>(data_[at + 2]) << 16 | static_cast<std::uint32_t>(data_[at + 3]) << 24;
    }

    Bytes From(std::size_t at) const { return Bytes(at < data_.size() ? data_.subspan(at) : std::span<const std::uint8_t>{}); }
    Bytes First(std::size_t count) const { return Bytes(data_.first(std::min(count, data_.size()))); }
    std::span<const std::uint8_t> Span() const { return data_; }

private:
    std::span<const std::uint8_t> data_;
};

bool Named(std::uint32_t word, std::uint32_t tag) {
    const std::uint32_t swapped = (tag >> 24) | (tag >> 8 & 0xFF00) | (tag << 8 & 0xFF0000) | (tag << 24);
    return word == tag || word == swapped;
}

bool ChunkAt(const Bytes &file, std::size_t at, std::uint32_t name) {
    return Named(file.U32(at), Tag("SCEI")) && Named(file.U32(at + 4), name);
}

// The first chunk of that name, searched word by word.
std::uint32_t FindChunk(const Bytes &file, std::uint32_t name) {
    for (std::size_t at = 0; at + 8 <= file.size(); at += 4) {
        if (ChunkAt(file, at, name)) {
            return static_cast<std::uint32_t>(at);
        }
    }
    return kNone;
}

// An HD's table: where the header says it is, or else wherever it's found; cut to its own size where that's sensible.
constexpr std::size_t kTableFirst = 0x10; // after the chunk's tag, size and highest index
Bytes HdTable(const Bytes &hd, std::uint32_t header, std::size_t field, std::uint32_t name) {
    std::uint32_t at = kNone;
    if (header != kNone) {
        const std::uint32_t pointed = hd.U32(header + field);
        if (pointed != kNone && ChunkAt(hd, pointed, name)) {
            at = pointed;
        }
    }
    if (at == kNone) {
        at = FindChunk(hd, name);
    }
    if (at == kNone) {
        return Bytes({});
    }
    const Bytes         table = hd.From(at);
    const std::uint32_t size = table.U32(8);
    return size >= kTableFirst && size <= table.size() ? table.First(size) : table;
}

// Each entry of a table (index 0 to its highest), given its bytes from where its offset points, or none where it has no place.
template <class Read>
void EachEntry(const Bytes &table, Read read) {
    if (table.size() < kTableFirst) {
        return;
    }
    const std::uint32_t highest = table.U32(0x0C);
    if (highest == kNone) {
        return;
    }
    const std::size_t entries = std::min<std::size_t>(std::size_t{highest} + 1, (table.size() - kTableFirst) / 4);
    for (std::size_t i = 0; i < entries; i++) {
        const std::uint32_t at = table.U32(kTableFirst + i * 4);
        read(at == kNone || at >= table.size() ? Bytes({}) : table.From(at));
    }
}

void ReadProgram(const Bytes &entry, na::BankProgram &program) {
    constexpr std::size_t kProgramBytes = 0x24;
    constexpr std::size_t kSplitBytes = 0x14;
    if (entry.size() < kProgramBytes) {
        return; // a program the bank leaves empty: never played, as the published documents leave it
    }
    program.present = true;
    const std::uint32_t splits_at = entry.U32(0);
    const int           splits = entry.U8(4);
    const std::size_t   stride = std::max<std::size_t>(entry.U8(5), kSplitBytes);
    program.volume = entry.U8(6);
    program.pan = entry.U8(7);
    program.transpose = entry.S8(8);
    program.detune = entry.S8(9);
    for (int i = 0; i < splits; i++) {
        const std::size_t at = splits_at + static_cast<std::size_t>(i) * stride;
        if (splits_at == kNone || at + kSplitBytes > entry.size()) {
            break;
        }
        program.splits.push_back({.key_low = entry.U8(at + 2),
                                  .key_high = entry.U8(at + 4),
                                  .sample_set = entry.U16(at),
                                  .transpose = entry.S8(at + 0x12),
                                  .detune = entry.S8(at + 0x13),
                                  .volume = entry.U8(at + 0x10),
                                  .pan = entry.U8(at + 0x11),
                                  .bend_low = entry.U16(at + 6),
                                  .bend_high = entry.U16(at + 8)});
    }
}

void ReadSampleSet(const Bytes &entry, na::BankSampleSet &set) {
    if (entry.size() < 4) {
        return;
    }
    set.velocity_curve = entry.U8(0);
    set.velocity_low = entry.U8(1);
    set.velocity_high = entry.U8(2);
    const int samples = entry.U8(3);
    for (int i = 0; i < samples && 6 + static_cast<std::size_t>(i) * 2 <= entry.size(); i++) {
        set.samples.push_back(entry.U16(4 + static_cast<std::size_t>(i) * 2));
    }
}

void ReadSample(const Bytes &entry, na::BankSample &sample) {
    constexpr std::size_t kSampleBytes = 0x2A;
    if (entry.size() < kSampleBytes) {
        return; // an empty sample, with no waveform: silent, as the published documents leave it
    }
    sample.wave = entry.U16(0);
    sample.velocity_low = entry.U8(2);
    sample.velocity_high = entry.U8(4);
    sample.base_note = entry.U8(0x0B);
    sample.detune = entry.S8(0x0C);
    sample.pan = entry.U8(0x0D);
    sample.volume = entry.U8(0x10);
    sample.adsr1 = entry.U16(0x12);
    sample.adsr2 = entry.U16(0x14);
    sample.spu_attribute = entry.U8(0x29);
}

// Sony's MIDI: a song's events, as the sound library's sequencer reads them, into Noktus's sequence.
class SongReader {
public:
    explicit SongReader(const Bytes &data) : data_(data) {}

    // Every event to the song's end (its end event, or its data's), the time of those Noktus doesn't take carried to the next.
    void ReadInto(na::Sequence &song, std::size_t most_events) {
        using Event = na::SequenceEvent;
        std::uint32_t carried = 0;
        for (std::size_t read = 0; read <= most_events; read++) {
            Next next;
            if (!Read(next)) {
                song.events.push_back({.delta = carried, .kind = Event::Kind::End});
                return;
            }
            const std::uint32_t delta = carried + next.delta;
            carried = 0;
            switch (next.kind) {
                case Next::Kind::Channel:
                    song.events.push_back({.delta = delta, .kind = Event::Kind::Channel, .status = next.status, .data1 = next.data[0], .data2 = next.data[1]});
                    break;
                case Next::Kind::Tempo:
                    song.events.push_back({.delta = delta, .kind = Event::Kind::Tempo, .tempo = next.tempo});
                    break;
                case Next::Kind::End:
                    song.events.push_back({.delta = delta, .kind = Event::Kind::End});
                    return;
                case Next::Kind::Other:
                    carried = delta;
                    break;
            }
        }
        overran_ = true;
    }

    bool Overran() const { return overran_; }

private:
    struct Next {
        enum class Kind : std::uint8_t { Channel, Tempo, End, Other };
        Kind                        kind = Kind::End;
        std::uint32_t               delta = 0;
        std::uint8_t                status = 0;
        std::array<std::uint8_t, 2> data{};
        std::uint32_t               tempo = 0;
    };

    // Up to four bytes of seven bits, the high bit saying another follows.
    bool Number(std::uint32_t &value) {
        value = 0;
        for (int i = 0; i < 4; i++) {
            if (at_ >= data_.size()) {
                return false;
            }
            const std::uint8_t byte = data_.U8(at_++);
            value = value << 7 | (byte & 0x7F);
            if ((byte & 0x80) == 0) {
                break;
            }
        }
        return true;
    }

    // False at the data's end, or where what's left is cut short.
    bool Read(Next &next) {
        while (true) {
            if (no_delta_) {
                next.delta = 0;
                no_delta_ = false;
            } else if (!Number(next.delta)) {
                return false;
            }
            if (at_ >= data_.size()) {
                return false;
            }
            std::uint8_t status = data_.U8(at_);
            if ((status & 0x80) != 0) {
                at_++;
            } else if (running_ != 0) {
                status = running_; // running status: the last channel message's
            } else {
                at_++; // a stray data byte, with nothing to run on: passed over, its delta with it
                continue;
            }
            if (status == 0xFF) {
                if (at_ >= data_.size()) {
                    return false;
                }
                const std::uint8_t kind = data_.U8(at_++);
                std::uint32_t      length = 0;
                if (!Number(length) || length > data_.size() - at_) {
                    return false;
                }
                next.kind = Next::Kind::Other;
                if (kind == 0x2F) {
                    next.kind = Next::Kind::End;
                } else if (kind == 0x51 && length >= 3) {
                    next.kind = Next::Kind::Tempo;
                    next.tempo = static_cast<std::uint32_t>(data_.U8(at_)) << 16 | static_cast<std::uint32_t>(data_.U8(at_ + 1)) << 8 | data_.U8(at_ + 2);
                }
                at_ += length;
                return true;
            }
            if (status == 0xF0 || status == 0xF7) {
                std::uint32_t length = 0;
                if (!Number(length) || length > data_.size() - at_) {
                    return false;
                }
                at_ += length;
                next.kind = Next::Kind::Other;
                return true;
            }
            if (status > 0xF0) {
                // The system common messages, with their data bytes.
                const std::size_t length = status == 0xF2 ? 2 : status == 0xF1 || status == 0xF3 ? 1 : 0;
                at_ = std::min(at_ + length, data_.size());
                next.kind = Next::Kind::Other;
                return true;
            }
            running_ = status;
            // Sony's note-off carries its note alone, without MIDI's velocity; program changes and channel pressure have one byte as MIDI's do.
            const int type = status & 0xF0;
            const std::size_t length = type == 0x80 || type == 0xC0 || type == 0xD0 ? 1 : 2;
            if (length > data_.size() - at_) {
                return false;
            }
            next.kind = Next::Kind::Channel;
            next.status = status;
            next.data = {};
            for (std::size_t i = 0; i < length; i++) {
                next.data[i] = data_.U8(at_++) & 0x7F;
            }
            // A last data byte with its high bit set says the next event comes at once, with no delta of its own.
            no_delta_ = (data_.U8(at_ - 1) & 0x80) != 0;
            return true;
        }
    }

    Bytes        data_;
    std::size_t  at_ = 0;
    std::uint8_t running_ = 0;
    bool         no_delta_ = false;
    bool         overran_ = false;
};

} // namespace

RetailWave DecodeRetailWave(std::span<const std::uint8_t> bd) {
    // The SPU2's ADPCM: blocks of 16 bytes, each 28 four-bit samples under a shift and one of five predicting filters, and flags marking the
    // loop's start and the wave's end.
    constexpr std::size_t                  kBlockBytes = 16;
    constexpr std::size_t                  kBlockSamples = 28;
    constexpr std::array<std::array<int, 2>, 5> kFilters = {
        {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}}
    };
    constexpr std::uint8_t kEnd = 0x01;
    constexpr std::uint8_t kRepeat = 0x02;
    constexpr std::uint8_t kLoopStart = 0x04;

    RetailWave        wave;
    const std::size_t blocks = bd.size() / kBlockBytes;
    wave.samples.reserve(blocks * kBlockSamples);
    std::array<int, 2> history{};
    for (std::size_t b = 0; b < blocks; b++) {
        const std::uint8_t *block = bd.data() + b * kBlockBytes;
        int                 shift = block[0] & 0x0F;
        if (shift > 12) {
            shift = 9; // as the hardware takes the shifts it has no use for
        }
        const std::array<int, 2> &filter = kFilters[std::min(block[0] >> 4, 4)];
        const std::uint8_t        flags = block[1];
        if ((flags & kLoopStart) != 0) {
            wave.loop_start = wave.samples.size();
        }
        for (std::size_t i = 0; i < kBlockSamples; i++) {
            const int nibble = (block[2 + i / 2] >> ((i & 1) != 0 ? 4 : 0)) & 0x0F;
            int       sample = static_cast<std::int16_t>(static_cast<std::uint16_t>(nibble << 12)) >> shift;
            sample += (history[0] * filter[0] + history[1] * filter[1] + 32) >> 6;
            sample = std::clamp(sample, -32768, 32767);
            history = {sample, history[0]};
            wave.samples.push_back(static_cast<float>(sample) / 32768.0f);
        }
        if ((flags & kEnd) != 0) {
            wave.loops = (flags & kRepeat) != 0;
            break;
        }
    }
    if (wave.loop_start >= wave.samples.size()) {
        wave.loops = false;
        wave.loop_start = wave.samples.empty() ? 0 : wave.samples.size() - 1;
    }
    return wave;
}

std::shared_ptr<const na::Bank> RetailBank(std::span<const std::uint8_t> hd_bytes, std::span<const std::uint8_t> bd) {
    // The header's fields point to its four tables.
    constexpr std::size_t kProgramsField = 0x14;
    constexpr std::size_t kSampleSetsField = 0x18;
    constexpr std::size_t kSamplesField = 0x1C;
    constexpr std::size_t kWavesField = 0x20;

    const Bytes         hd(hd_bytes);
    const std::uint32_t header = FindChunk(hd, Tag("Head"));
    const Bytes         programs = HdTable(hd, header, kProgramsField, Tag("Prog"));
    const Bytes         sample_sets = HdTable(hd, header, kSampleSetsField, Tag("Sset"));
    const Bytes         samples = HdTable(hd, header, kSamplesField, Tag("Smpl"));
    const Bytes         waves = HdTable(hd, header, kWavesField, Tag("Vagi"));
    if (programs.size() == 0 || samples.size() == 0 || waves.size() == 0) {
        return nullptr;
    }
    auto bank = std::make_shared<na::Bank>();
    EachEntry(programs, [&](const Bytes &entry) { ReadProgram(entry, bank->programs.emplace_back()); });
    EachEntry(sample_sets, [&](const Bytes &entry) { ReadSampleSet(entry, bank->sample_sets.emplace_back()); });
    EachEntry(samples, [&](const Bytes &entry) { ReadSample(entry, bank->samples.emplace_back()); });
    EachEntry(waves, [&](const Bytes &entry) {
        na::BankWave &wave = bank->waves.emplace_back();
        // Where it starts in the BD, and its rate.
        const std::uint32_t at = entry.size() >= 8 ? entry.U32(0) : kNone;
        const int           rate = entry.size() >= 8 ? entry.U16(4) : 0;
        if (at == kNone || at >= bd.size() || rate <= 0) {
            return;
        }
        RetailWave decoded = DecodeRetailWave(bd.subspan(at));
        if (decoded.samples.empty()) {
            return;
        }
        auto samples_of = std::make_shared<na::Samples>();
        samples_of->rate = rate;
        samples_of->data = std::move(decoded.samples);
        wave.samples = std::move(samples_of);
        wave.loop_start = decoded.loop_start;
        wave.loops = decoded.loops;
    });
    return bank;
}

std::vector<std::shared_ptr<const na::Sequence>> RetailSequences(std::span<const std::uint8_t> sq_bytes) {
    const Bytes         sq(sq_bytes);
    const std::uint32_t midi = FindChunk(sq, Tag("Midi"));
    if (midi == kNone) {
        return {};
    }
    // The chunk's size, the highest song's index, then each song's offset from the chunk; a song runs to the next one's start or the chunk's end.
    const std::uint32_t size = sq.U32(midi + 8);
    if (size < 0x10 || size > sq.size() - midi) {
        return {};
    }
    const std::size_t   end = midi + std::size_t{size};
    const std::uint32_t highest = sq.U32(midi + 0x0C);
    if (highest == kNone || std::size_t{highest} + 1 > (end - midi - 0x10) / 4) {
        return {};
    }
    const std::size_t        count = std::size_t{highest} + 1;
    std::vector<std::size_t> starts(count, 0);
    for (std::size_t i = 0; i < count; i++) {
        const std::uint32_t offset = sq.U32(midi + 0x10 + i * 4);
        starts[i] = offset != kNone && midi + std::size_t{offset} + 6 <= end ? midi + offset : 0;
    }

    std::vector<std::shared_ptr<const na::Sequence>> songs;
    for (std::size_t i = 0; i < count; i++) {
        auto song = std::make_shared<na::Sequence>();
        song->division = 480;
        const std::size_t start = starts[i];
        if (start != 0) {
            std::size_t song_end = end;
            for (const std::size_t other : starts) {
                if (other > start) {
                    song_end = std::min(song_end, other);
                }
            }
            // The song's own header: where its events start (at least past this header) and its ticks a beat.
            std::size_t events_at = sq.U32(start);
            if (events_at < 6) {
                events_at = 6;
            } else if (events_at > song_end - start) {
                return {};
            }
            const int division = sq.U16(start + 4);
            if (division > 0 && division < 0x8000) {
                song->division = division;
            }
            SongReader reader(sq.From(start + events_at).First(song_end - start - events_at));
            reader.ReadInto(*song, sq.size()); // no song has more events than the file has bytes: one that seems to is broken
            if (reader.Overran()) {
                return {};
            }
        }
        songs.push_back(std::move(song));
    }
    return songs;
}

} // namespace dc::sound
