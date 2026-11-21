// nlohmann/json before the game headers, whose one-letter macros it cannot stand.
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "dataread.hpp"
#include "platform/paths.hpp"
#include "snd.hpp"
#include "sound.hpp"
#include "sound/published.hpp"
#include "sound/retail.hpp"
#include "sound_data.hpp"

namespace dc::test {

using namespace dc::game;

namespace {

namespace fs = std::filesystem;
namespace sound = dcdata::sound;

std::string ReadText(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::span<const std::uint8_t> Bytes(const dcdata::PackMember &member) {
    return member.data;
}

namespace na = noktus::audio;

// What the synth plays from either: every program, split, sample set and sample, and every waveform's samples and loop.
void ExpectSameBank(const na::Bank &raw, const na::Bank &published, const std::string &where) {
    ASSERT_EQ(raw.programs.size(), published.programs.size()) << where;
    for (std::size_t p = 0; p < raw.programs.size(); p++) {
        const na::BankProgram &x = raw.programs[p];
        const na::BankProgram &y = published.programs[p];
        ASSERT_EQ(x.present, y.present) << where << " program " << p;
        EXPECT_EQ(std::tie(x.volume, x.pan, x.transpose, x.detune), std::tie(y.volume, y.pan, y.transpose, y.detune)) << where << " program " << p;
        ASSERT_EQ(x.splits.size(), y.splits.size()) << where << " program " << p;
        for (std::size_t s = 0; s < x.splits.size(); s++) {
            const na::BankSplit &i = x.splits[s];
            const na::BankSplit &j = y.splits[s];
            EXPECT_EQ(std::tie(i.sample_set, i.key_low, i.key_high, i.bend_low, i.bend_high, i.volume, i.pan, i.transpose, i.detune),
                      std::tie(j.sample_set, j.key_low, j.key_high, j.bend_low, j.bend_high, j.volume, j.pan, j.transpose, j.detune))
                << where << " program " << p << " split " << s;
        }
    }
    ASSERT_EQ(raw.sample_sets.size(), published.sample_sets.size()) << where;
    for (std::size_t s = 0; s < raw.sample_sets.size(); s++) {
        const na::BankSampleSet &i = raw.sample_sets[s];
        const na::BankSampleSet &j = published.sample_sets[s];
        EXPECT_EQ(i.samples, j.samples) << where << " sample set " << s;
        // An empty set plays nothing, whatever velocities it's given (the documents give an empty place's 0 to 0).
        if (!i.samples.empty()) {
            EXPECT_EQ(std::tie(i.velocity_low, i.velocity_high, i.velocity_curve), std::tie(j.velocity_low, j.velocity_high, j.velocity_curve))
                << where << " sample set " << s;
        }
    }
    ASSERT_EQ(raw.samples.size(), published.samples.size()) << where;
    for (std::size_t s = 0; s < raw.samples.size(); s++) {
        const na::BankSample &i = raw.samples[s];
        const na::BankSample &j = published.samples[s];
        EXPECT_EQ(std::tie(i.wave, i.velocity_low, i.velocity_high, i.base_note, i.detune, i.pan, i.volume, i.adsr1, i.adsr2, i.spu_attribute),
                  std::tie(j.wave, j.velocity_low, j.velocity_high, j.base_note, j.detune, j.pan, j.volume, j.adsr1, j.adsr2, j.spu_attribute))
            << where << " sample " << s;
    }
    ASSERT_EQ(raw.waves.size(), published.waves.size()) << where;
    for (std::size_t w = 0; w < raw.waves.size(); w++) {
        const na::BankWave &i = raw.waves[w];
        const na::BankWave &j = published.waves[w];
        ASSERT_EQ(i.samples == nullptr, j.samples == nullptr) << where << " wave " << w;
        if (i.samples != nullptr) {
            EXPECT_EQ(i.samples->rate, j.samples->rate) << where << " wave " << w;
            EXPECT_EQ(i.samples->data, j.samples->data) << where << " wave " << w;
            EXPECT_EQ(std::tie(i.loop_start, i.loops), std::tie(j.loop_start, j.loops)) << where << " wave " << w;
        }
    }
}

void ExpectSameSong(const na::Sequence &raw, const na::Sequence &published, const std::string &where) {
    EXPECT_EQ(raw.division, published.division) << where;
    ASSERT_EQ(raw.events.size(), published.events.size()) << where;
    for (std::size_t e = 0; e < raw.events.size(); e++) {
        const na::SequenceEvent &x = raw.events[e];
        const na::SequenceEvent &y = published.events[e];
        EXPECT_EQ(std::tie(x.delta, x.kind, x.status, x.data1, x.data2, x.tempo), std::tie(y.delta, y.kind, y.status, y.data1, y.data2, y.tempo))
            << where << " event " << e;
    }
}

} // namespace

TEST(SoundPublish, ALoadListNamesBanksAndSequencesByTheirFirstNineCharacters) {
    const auto entries = sound::ReadLoadList("e01a_c.hd\r\n.sq\nshort\ne01a_e.sq trailing\nnotes.txt\n\no01b_a.hd");
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].member, "e01a_c.hd");
    EXPECT_EQ(entries[0].slot, 'c');
    EXPECT_EQ(entries[0].kind, sound::LoadKind::Bank);
    EXPECT_EQ(entries[1].member, "e01a_e.sq");
    EXPECT_EQ(entries[1].kind, sound::LoadKind::Sequence);
    EXPECT_EQ(entries[2].member, "o01b_a.hd");
    EXPECT_EQ(entries[2].slot, 'a');
}

TEST(SoundPublish, APackConfigReadsItsTwoCommandsAndLeavesTheRestZero) {
    const auto both = sound::ReadPackConfig("REVERBE 2,3\nTABLE 1, 4\n\n");
    EXPECT_EQ(both.reverb_mode, 2);
    EXPECT_EQ(both.reverb_depth, 3);
    EXPECT_EQ(both.se_table, 1);
    EXPECT_EQ(both.se_table_type, 4);
    const auto reverb = sound::ReadPackConfig("REVERBE 4,30\n");
    EXPECT_EQ(reverb.reverb_mode, 4);
    EXPECT_EQ(reverb.reverb_depth, 30);
    EXPECT_EQ(reverb.se_table, 0);
    EXPECT_EQ(sound::ReadPackConfig("").reverb_mode, 0);
}

TEST(SoundPublish, TheGlobalTablesSkipTheirHeaderAndReadAsTheGameDoes) {
    const auto sequences = sound::ReadSequenceVolumes("filename vol\nb01a_a.sq\t94\nlongername1.sq 12;\n");
    ASSERT_EQ(sequences.size(), 2u);
    EXPECT_EQ(sequences[0].name, "b01a_a.sq");
    EXPECT_EQ(sequences[0].volume, 94);
    EXPECT_EQ(sequences[1].name, "longernam");
    const auto effects = sound::ReadEffectVolumes("PRG KEY VOL\n16\t20\t38\n123456,7,8\n");
    ASSERT_EQ(effects.size(), 2u);
    EXPECT_EQ(effects[0].bank, 16);
    EXPECT_EQ(effects[0].program, 20);
    EXPECT_EQ(effects[0].volume, 38);
    EXPECT_EQ(effects[1].bank, 1234);
}

TEST(SoundPublish, SharedBanksAreNamedForTheirMemberAndContents) {
    const std::array<unsigned char, 3> hd = {1, 2, 3};
    const std::array<unsigned char, 2> bd = {4, 5};
    const std::array<unsigned char, 2> other = {4, 6};
    const std::string                  key = sound::SharedBankKey("E01A_C.hd", hd, bd);
    EXPECT_TRUE(key.starts_with("sound/banks/e01a_c-"));
    EXPECT_TRUE(key.ends_with(".hd"));
    EXPECT_EQ(key, sound::SharedBankKey("E01A_C.hd", hd, bd));
    EXPECT_NE(key, sound::SharedBankKey("E01A_C.hd", hd, other));
    EXPECT_TRUE(sound::SharedBankKey(".hd", hd, bd).starts_with("sound/banks/unnamed-"));
    EXPECT_TRUE(sound::SharedBankKey("\x81\x9b.hd", hd, bd).starts_with("sound/banks/__-"));
}

TEST(SoundPublish, AMemberIsTheFirstWhoseNameMatchesIgnoringCase) {
    const std::vector<dcdata::PackMember> members = {
        {"s04a_e.sq", {1}},
        {"S04A_E.SQ", {2}},
    };
    const dcdata::PackMember *found = sound::FindMember(members, "S04a_e.sq");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->data[0], 1);
    EXPECT_EQ(sound::FindMember(members, "missing.sq"), nullptr);
}

// DC_DATA is a data folder `dcdata publish-sound` has published sound into.
TEST(SoundPublishRealData, EveryPublishedPackPlaysWhatTheOriginalsDo) {
    const char *env = std::getenv("DC_DATA");
    if (env == nullptr || *env == '\0') {
        GTEST_SKIP() << "DC_DATA is not set";
    }
    const fs::path        data = env;
    std::vector<fs::path> documents;
    for (const auto &entry : fs::recursive_directory_iterator(data)) {
        if (entry.is_regular_file() && entry.path().filename().string().ends_with(".sound.json")) {
            documents.push_back(entry.path());
        }
    }
    if (documents.empty()) {
        GTEST_SKIP() << "no published sound in DC_DATA; run dcdata publish-sound";
    }
    const auto read = [&](const std::string &path) -> std::optional<std::vector<std::uint8_t>> {
        std::ifstream stream(data / path, std::ios::binary);
        if (!stream) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    };
    int        banks = 0;
    int        sequences = 0;
    int        configs = 0;
    for (const fs::path &document : documents) {
        // A document sits beside its pack: a .snd, or a .pak that holds sound among other things.
        fs::path stem = document;
        stem.replace_extension().replace_extension();
        fs::path pack_path;
        for (const char *extension : {".snd", ".pak"}) {
            if (fs::exists(fs::path(stem).replace_extension(extension))) {
                pack_path = fs::path(stem).replace_extension(extension);
                break;
            }
        }
        ASSERT_FALSE(pack_path.empty()) << document << " has no pack beside it";
        const auto members = dcdata::ReadPack(dcdata::ReadFile(pack_path));
        const auto json = nlohmann::json::parse(ReadText(document));
        ASSERT_EQ(json["kind"], "sound_pack") << document;
        for (const auto &list : json["lists"]) {
            const std::string name = list["name"];
            for (const auto &load : list["load"]) {
                const std::string         member = load["member"];
                const std::string         where = document.string() + " " + member;
                const dcdata::PackMember *source = sound::FindMember(members, member);
                ASSERT_NE(source, nullptr) << where;
                if (load.contains("bank")) {
                    const dcdata::PackMember *bd = sound::FindMember(members, member.substr(0, 7) + "bd");
                    ASSERT_NE(bd, nullptr) << where;
                    const auto raw = dc::sound::RetailBank(Bytes(*source), Bytes(*bd));
                    const auto published = dc::sound::NoktusBankFromJson(ReadText(data / load["bank"].get<std::string>()), read);
                    ASSERT_NE(raw, nullptr) << where;
                    ASSERT_NE(published, nullptr) << where;
                    ExpectSameBank(*raw, *published, where);
                    banks++;
                } else {
                    const auto        raw = dc::sound::RetailSequences(Bytes(*source));
                    const std::string text = ReadText(data / load["sequence"].get<std::string>());
                    ASSERT_FALSE(raw.empty()) << where;
                    for (std::size_t song = 0; song < raw.size(); song++) {
                        const auto published = dc::sound::NoktusSequenceFromJson(text, static_cast<int>(song));
                        ASSERT_NE(published, nullptr) << where << " song " << song;
                        ExpectSameSong(*raw[song], *published, where + " song " + std::to_string(song));
                    }
                    EXPECT_EQ(dc::sound::NoktusSequenceFromJson(text, static_cast<int>(raw.size())), nullptr) << where;
                    sequences++;
                }
            }
            const dcdata::PackMember *cfg = sound::FindMember(members, name.substr(0, name.find('.')) + ".cfg");
            ASSERT_EQ(cfg != nullptr, !list["config"].is_null()) << document << " " << name;
            if (cfg != nullptr) {
                std::vector<char> script(cfg->data.begin(), cfg->data.end());
                SND_INFO          info{};
                LoadSoundInfo(&info, script.data(), static_cast<int>(script.size()));
                EXPECT_EQ(list["config"]["reverb_mode"], info.reverb_mode) << document << " " << name;
                EXPECT_EQ(list["config"]["reverb_depth"], info.reverb_depth) << document << " " << name;
                EXPECT_EQ(list["config"]["se_table"], info.se_table) << document << " " << name;
                EXPECT_EQ(list["config"]["se_table_type"], info.se_table_type) << document << " " << name;
                configs++;
            }
        }
    }
    EXPECT_GT(banks, 0);
    EXPECT_GT(sequences, 0);
    EXPECT_GT(configs, 0);
    std::printf("checked %d bank loads, %d sequence loads and %d .cfg files in %zu packs\n", banks, sequences, configs, documents.size());

    // The sequence volumes' reader has no accessor; the table test above covers ReadSequenceVolumes.
    if (!fs::exists(data / "data.hd2")) {
        return;
    }
    PathsSetDataRoot(data);
    InitCDFile();
    std::vector<unsigned int> buffer(1 << 20);
    CSnd.LoadSeInf(const_cast<char *>("sound/tbl/setbl.txt"), buffer.data());
    const auto *effects = reinterpret_cast<const SE_INF_TABLE *>(CSnd.GetSeInfTbl());
    const auto  effect_table = nlohmann::json::parse(ReadText(data / "sound/tbl/setbl.json"));
    ASSERT_EQ(effect_table["rows"].size(), static_cast<std::size_t>(effects->count));
    for (int i = 0; i < effects->count; i++) {
        EXPECT_EQ(effect_table["rows"][i]["bank"], effects->entry[i].bank) << i;
        EXPECT_EQ(effect_table["rows"][i]["program"], effects->entry[i].program) << i;
        EXPECT_EQ(effect_table["rows"][i]["volume"], effects->entry[i].volume) << i;
    }
}

} // namespace dc::test
