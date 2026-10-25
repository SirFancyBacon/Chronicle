#pragma once

// Dark Cloud's sound data as the extractor publishes it (docs/AUDIO_FRAMEWORK.md, section 4). Kazarov
// converts the banks and sequences; this reads what only this game has, as the game reads it: a sound
// pack's load lists and their .cfg commands, and the global volume tables. Header-only and free of
// game headers, like dcdata.hpp, so the port's tests can use it.

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dcdata.hpp"

namespace dcdata::sound {

enum class LoadKind { Bank, Sequence };

// One member a load list names.
struct LoadEntry {
    std::string member; // the name the game asks the pack for: "e01a_c.hd"
    char        slot;   // the name's sixth letter, which picks the driver slot
    LoadKind    kind;
};

// A load list (a pack's .txt) as CSound::LoadSoundFileFromPack reads it: each line of nine
// characters or more names a member by its first nine, a bank (".hd") or a sequence (".sq"); other
// lines name nothing.
inline std::vector<LoadEntry> ReadLoadList(std::string_view text) {
    text = text.substr(0, text.find('\0'));
    std::vector<LoadEntry> entries;
    std::size_t            start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (line.size() < 9) {
            continue;
        }
        const std::string_view member = line.substr(0, 9);
        if (member.substr(6) == ".hd") {
            entries.push_back({std::string(member), member[5], LoadKind::Bank});
        } else if (member.substr(6) == ".sq") {
            entries.push_back({std::string(member), member[5], LoadKind::Sequence});
        }
    }
    return entries;
}

// What a .cfg sets, as LoadSoundInfo leaves it: zero where a command is missing.
struct PackConfig {
    int reverb_mode = 0;
    int reverb_depth = 0;
    int se_table = 0;
    int se_table_type = 0;
};

// The REVERBE and TABLE commands of a .cfg, each followed by two integers. The game reads these files
// with its script interpreter; the port's tests check every .cfg on the disc against it.
inline PackConfig ReadPackConfig(std::string_view text) {
    text = text.substr(0, text.find('\0'));
    PackConfig  config;
    std::size_t at = 0;
    auto        number = [&]() {
        while (at < text.size() && !(std::isdigit(static_cast<unsigned char>(text[at])) || text[at] == '-')) {
            at++;
        }
        const std::size_t begin = at;
        if (at < text.size() && text[at] == '-') {
            at++;
        }
        while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at]))) {
            at++;
        }
        return std::atoi(std::string(text.substr(begin, at - begin)).c_str());
    };
    while (at < text.size()) {
        if (!std::isalpha(static_cast<unsigned char>(text[at]))) {
            at++;
            continue;
        }
        const std::size_t begin = at;
        while (at < text.size() && std::isalnum(static_cast<unsigned char>(text[at]))) {
            at++;
        }
        const std::string_view word = text.substr(begin, at - begin);
        if (word == "REVERBE") {
            config.reverb_mode = number();
            config.reverb_depth = number();
        } else if (word == "TABLE") {
            config.se_table = number();
            config.se_table_type = number();
        }
    }
    return config;
}

// A global table's values as CSound's table readers take them: whitespace, commas and semicolons
// separate them, and the first line is a header.
inline std::vector<std::string> ReadTableTokens(std::string_view text) {
    text = text.substr(0, text.find('\0'));
    std::vector<std::string> tokens;
    auto                     delimiter = [](char c) {
        return c == ' ' || c == ';' || c == ',' || c == '\t' || c == '\n' || c == '\r';
    };
    std::size_t at = text.find_first_of("\n\r");
    while (at < text.size()) {
        while (at < text.size() && delimiter(text[at])) {
            at++;
        }
        const std::size_t begin = at;
        while (at < text.size() && !delimiter(text[at])) {
            at++;
        }
        if (at > begin) {
            tokens.emplace_back(text.substr(begin, at - begin));
        }
    }
    return tokens;
}

struct SequenceVolume {
    std::string name; // up to nine characters, as MIDI_SEQUENCE keeps it
    int         volume;
};

// sound/tbl/sqtbl.txt as CSound::LoadSqInf reads it: name and volume pairs, at most 400.
inline std::vector<SequenceVolume> ReadSequenceVolumes(std::string_view text) {
    const auto                  tokens = ReadTableTokens(text);
    std::vector<SequenceVolume> rows;
    for (std::size_t i = 0; i + 1 < tokens.size() && rows.size() < 400; i += 2) {
        rows.push_back({tokens[i].substr(0, 9), std::atoi(tokens[i + 1].c_str())});
    }
    return rows;
}

struct EffectVolume {
    int bank;
    int program;
    int volume;
};

// sound/tbl/setbl.txt as CSound::LoadSeInf reads it: bank, program and volume, each the number in
// its first four characters, at most 3,000 rows.
inline std::vector<EffectVolume> ReadEffectVolumes(std::string_view text) {
    const auto tokens = ReadTableTokens(text);
    auto       number = [](const std::string &token) { return std::atoi(token.substr(0, 4).c_str()); };
    std::vector<EffectVolume> rows;
    for (std::size_t i = 0; i + 2 < tokens.size() && rows.size() < 3000; i += 3) {
        rows.push_back({number(tokens[i]), number(tokens[i + 1]), number(tokens[i + 2])});
    }
    return rows;
}

// A JSON string. Bytes past ASCII become \u00XX, so a reader that maps code points below 256 back to
// bytes gets the disc's own bytes.
inline std::string JsonString(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (byte < 0x20 || byte >= 0x7F) {
            out += std::format("\\u{:04x}", byte);
        } else {
            out += c;
        }
    }
    return out + "\"";
}

// Where a bank is published: once per distinct HD and BD pair, named for the first member that
// holds it and its contents' hash, since the same bank sits in many packs.
inline std::string SharedBankKey(std::string_view member, std::span<const unsigned char> hd,
                                 std::span<const unsigned char> bd) {
    std::vector<unsigned char> both(hd.begin(), hd.end());
    both.insert(both.end(), bd.begin(), bd.end());
    std::string stem;
    for (const char c : member.substr(0, member.rfind('.'))) {
        const auto byte = static_cast<unsigned char>(c);
        stem += std::isalnum(byte) || c == '_' ? static_cast<char>(std::tolower(byte)) : '_';
    }
    if (stem.empty()) {
        stem = "unnamed";
    }
    return std::format("sound/banks/{}-{:016x}.hd", stem, Fnv1a64(both));
}

// The member of a pack the game would find by name: the first whose name matches, ignoring case.
inline const PackMember *FindMember(const std::vector<PackMember> &members, std::string_view name) {
    for (const PackMember &member : members) {
        if (EqualsFolded(member.name, name)) {
            return &member;
        }
    }
    return nullptr;
}

} // namespace dcdata::sound
