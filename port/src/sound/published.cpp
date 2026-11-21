// nlohmann/json before the game headers, whose one-letter macros it cannot stand.
#include "sound/published.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "audio/trace.hpp"
#include "data/data_mount.hpp"
#include "data/data_read.hpp"
#include "platform/datapacks.hpp"

namespace dc::sound {

using Json = nlohmann::json;

namespace {

std::string Lower(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 0x20 : c); });
    return out;
}

// Where a published file is found: each active datapack's, highest priority first, then the data's.
std::vector<vfs::Ref> Candidates(std::string_view path) {
    std::vector<vfs::Ref> found = platform::datapacks::Find(path);
    if (vfs::Ref own = vfs::Find(data::DataMount(), path)) {
        found.push_back(std::move(own));
    }
    return found;
}

// A published document: the first candidate that is JSON of the kind asked for.
std::optional<Json> Document(std::string_view path, std::string_view kind) {
    for (const vfs::Ref &file : Candidates(path)) {
        std::vector<std::uint8_t> bytes;
        Json                      document = vfs::Read(file, bytes) ? Json::parse(bytes, nullptr, false) : Json(Json::value_t::discarded);
        if (!document.is_discarded() && document.is_object() && document.value("kind", "") == kind) {
            return document;
        }
        // Each broken file is reported once; the next candidate, or the disc's own, plays.
        static std::set<std::string> reported;
        if (reported.insert(file.Display()).second) {
            std::fprintf(stderr, "sound: %s is not a %.*s document\n", file.Display().c_str(), static_cast<int>(kind.size()), kind.data());
        }
    }
    return std::nullopt;
}

std::optional<std::string> Text(std::string_view path) {
    for (const vfs::Ref &file : Candidates(path)) {
        std::vector<std::uint8_t> bytes;
        if (vfs::Read(file, bytes)) {
            return std::string(bytes.begin(), bytes.end());
        }
    }
    return std::nullopt;
}

int Int(const Json &object, const char *key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_number_integer() ? found->get<int>() : 0;
}

// The published table beside a text table: sound/tbl/sqtbl.txt has sound/tbl/sqtbl.json.
std::optional<Json> TableRows(std::string_view table, std::string_view kind) {
    std::filesystem::path path(table);
    path.replace_extension(".json");
    std::optional<Json> document = Document(path.generic_string(), kind);
    if (!document || !document->contains("rows") || !(*document)["rows"].is_array()) {
        return std::nullopt;
    }
    return (*document)["rows"];
}

} // namespace

std::optional<PublishedList> FindList(const void *member, std::size_t size, std::string_view list) {
    data::DataReadSource source;
    if (member == nullptr || size == 0 || !data::DataReadSourceRange(member, size, source)) {
        return std::nullopt;
    }
    std::filesystem::path path(source.path);
    path.replace_extension(".sound.json");
    const std::string         document_path = path.generic_string();
    const std::optional<Json> document = Document(document_path, "sound_pack");
    if (!document || !document->contains("lists") || !(*document)["lists"].is_array()) {
        return std::nullopt;
    }
    for (const Json &entry : (*document)["lists"]) {
        if (!entry.is_object() || Lower(entry.value("name", "")) != Lower(list)) {
            continue;
        }
        PublishedList published{.document = document_path};
        if (const auto config = entry.find("config"); config != entry.end() && config->is_object() && !config->empty()) {
            published.config = PublishedConfig{
                .reverb_mode = Int(*config, "reverb_mode"),
                .reverb_depth = Int(*config, "reverb_depth"),
                .se_table = Int(*config, "se_table"),
                .se_table_type = Int(*config, "se_table_type"),
            };
        }
        for (const Json &load : entry.value("load", Json::array())) {
            const std::string name = Lower(load.value("member", ""));
            if (load.contains("bank") && load["bank"].is_string()) {
                published.banks[name] = load["bank"];
            } else if (load.contains("sequence") && load["sequence"].is_string()) {
                published.sequences[name] = load["sequence"];
            }
        }
        audio::Trace("published: %s list %.*s, %zu banks, %zu sequences", document_path.c_str(), static_cast<int>(list.size()), list.data(),
                     published.banks.size(), published.sequences.size());
        return published;
    }
    return std::nullopt;
}

std::shared_ptr<audio::Bank> LoadBank(const std::string &document) {
    const std::optional<std::string> text = Text(document);
    if (!text) {
        return nullptr;
    }
    return audio::Bank::CreateFromJsonCandidates(*text, [](const std::filesystem::path &sample) { return Candidates(sample.generic_string()); });
}

namespace {

// A WAV of 16-bit samples, one channel: its rate and values as floats (a value times 32768 is the 16-bit one). Nothing for anything else.
std::optional<noktus::audio::Samples> ReadWav(const std::vector<std::uint8_t> &bytes) {
    const auto word = [&](std::size_t at) { return static_cast<std::uint32_t>(bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | bytes[at + 3] << 24); };
    const auto half = [&](std::size_t at) { return static_cast<std::uint16_t>(bytes[at] | bytes[at + 1] << 8); };
    if (bytes.size() < 12 || std::string_view(reinterpret_cast<const char *>(bytes.data()), 4) != "RIFF" ||
        std::string_view(reinterpret_cast<const char *>(bytes.data()) + 8, 4) != "WAVE") {
        return std::nullopt;
    }
    int         rate = 0;
    bool        fitting = false;
    std::size_t at = 12;
    while (at + 8 <= bytes.size()) {
        const std::string_view id(reinterpret_cast<const char *>(bytes.data()) + at, 4);
        const std::size_t      size = word(at + 4);
        const std::size_t      body = at + 8;
        if (body + size > bytes.size()) {
            return std::nullopt;
        }
        if (id == "fmt " && size >= 16) {
            fitting = half(body) == 1 && half(body + 2) == 1 && half(body + 14) == 16;
            rate = static_cast<int>(word(body + 4));
        } else if (id == "data") {
            if (!fitting || rate <= 0) {
                return std::nullopt;
            }
            noktus::audio::Samples samples;
            samples.rate = rate;
            samples.channels = 1;
            samples.data.resize(size / 2);
            for (std::size_t i = 0; i < samples.data.size(); i++) {
                samples.data[i] = static_cast<float>(static_cast<std::int16_t>(half(body + i * 2))) / 32768.0f;
            }
            return samples;
        }
        at = body + size + (size & 1);
    }
    return std::nullopt;
}

std::shared_ptr<const noktus::audio::Bank> NoktusBankFromJsonOrThrow(std::string_view                                                                    text,
                                                                     const std::function<std::optional<std::vector<std::uint8_t>>(const std::string &)> &read) {
    namespace na = noktus::audio;
    const Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object() || root.value("kind", "") != "bank") {
        return nullptr;
    }
    auto       bank = std::make_shared<na::Bank>();
    const auto number = [](const Json &object, const char *key, int otherwise) {
        const auto found = object.find(key);
        return found != object.end() && found->is_number_integer() ? found->get<int>() : otherwise;
    };
    for (const Json &p : root.value("programs", Json::array())) {
        na::BankProgram &program = bank->programs.emplace_back();
        if (!p.is_object()) {
            continue;
        }
        program.present = p.value("present", false);
        if (!program.present) {
            continue;
        }
        program.transpose = number(p, "transpose", 0);
        program.detune = number(p, "detune", 0);
        program.volume = number(p, "volume", 128);
        program.pan = number(p, "pan", 64);
        for (const Json &s : p.value("splits", Json::array())) {
            program.splits.push_back({.key_low = number(s, "key_low", 0),
                                      .key_high = number(s, "key_high", 127),
                                      .sample_set = number(s, "sample_set", -1),
                                      .transpose = number(s, "transpose", 0),
                                      .detune = number(s, "detune", 0),
                                      .volume = number(s, "volume", 128),
                                      .pan = number(s, "pan", 64),
                                      .bend_low = number(s, "bend_low", 0),
                                      .bend_high = number(s, "bend_high", 0)});
        }
    }
    for (const Json &s : root.value("sample_sets", Json::array())) {
        na::BankSampleSet &set = bank->sample_sets.emplace_back();
        set.velocity_low = number(s, "vel_low", 0);
        set.velocity_high = number(s, "vel_high", 127);
        set.velocity_curve = number(s, "vel_curve", 0);
        for (const Json &index : s.value("samples", Json::array())) {
            set.samples.push_back(index.is_number_integer() ? index.get<int>() : -1);
        }
    }
    for (const Json &s : root.value("samples", Json::array())) {
        na::BankSample &sample = bank->samples.emplace_back();
        sample.wave = number(s, "vag", -1);
        sample.base_note = number(s, "base_note", 60);
        sample.detune = number(s, "detune", 0);
        sample.volume = number(s, "volume", 128);
        sample.pan = number(s, "pan", 64);
        sample.adsr1 = static_cast<std::uint16_t>(number(s, "adsr1", 0x00FF));
        sample.adsr2 = static_cast<std::uint16_t>(number(s, "adsr2", 0));
        sample.spu_attribute = static_cast<std::uint8_t>(number(s, "spu_attr", 0x0F)); // without one, heard dry and in the reverb, either core
        sample.velocity_low = number(s, "vel_low", 0);
        sample.velocity_high = number(s, "vel_high", 127);
    }
    for (const Json &v : root.value("vags", Json::array())) {
        na::BankWave &wave = bank->waves.emplace_back();
        if (!v.is_object() || !v.contains("wav") || !v["wav"].is_string()) {
            continue;
        }
        // A path within the data, never out of it.
        const std::filesystem::path relative = v["wav"].get<std::string>();
        if (relative.is_absolute() || std::ranges::any_of(relative, [](const std::filesystem::path &part) { return part == ".."; })) {
            return nullptr;
        }
        const std::optional<std::vector<std::uint8_t>> bytes = read(relative.generic_string());
        std::optional<na::Samples>                     samples = bytes ? ReadWav(*bytes) : std::nullopt;
        const int                                      loop_start = number(v, "loop_start", 0);
        const int                                      loop_end = number(v, "loop_end", 0);
        if (!samples || loop_end <= 0 || static_cast<std::size_t>(loop_end) > samples->data.size() || loop_start < 0 || loop_start >= loop_end) {
            return nullptr;
        }
        samples->data.resize(static_cast<std::size_t>(loop_end)); // the waveform ends where its loop does
        wave.samples = std::make_shared<const na::Samples>(std::move(*samples));
        wave.loop_start = static_cast<std::size_t>(loop_start);
        wave.loops = v.value("loops", false);
    }
    return bank;
}

} // namespace

// A bank document from a datapack may be anything: a key of the wrong kind throws in the reading, which here means no bank.
std::shared_ptr<const noktus::audio::Bank> NoktusBankFromJson(std::string_view                                                                    text,
                                                              const std::function<std::optional<std::vector<std::uint8_t>>(const std::string &)> &read) {
    try {
        return NoktusBankFromJsonOrThrow(text, read);
    } catch (const nlohmann::json::exception &) {
        return nullptr;
    }
}

std::shared_ptr<const noktus::audio::Bank> LoadNoktusBank(const std::string &document) {
    const std::optional<std::string> text = Text(document);
    if (!text) {
        return nullptr;
    }
    return NoktusBankFromJson(*text, [](const std::string &sample) -> std::optional<std::vector<std::uint8_t>> {
        for (const vfs::Ref &file : Candidates(sample)) {
            std::vector<std::uint8_t> bytes;
            if (vfs::Read(file, bytes)) {
                return bytes;
            }
        }
        return std::nullopt;
    });
}

namespace {

std::shared_ptr<const noktus::audio::Sequence> NoktusSequenceFromJsonOrThrow(std::string_view text, int song) {
    using Event = noktus::audio::SequenceEvent;
    const Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object() || root.value("kind", "") != "sequence" || !root.contains("songs") || !root["songs"].is_array() ||
        song < 0 || song >= static_cast<int>(root["songs"].size()) || !root["songs"][song].is_object()) {
        return nullptr;
    }
    const Json &source = root["songs"][song];
    auto        sequence = std::make_shared<noktus::audio::Sequence>();
    sequence->division = source.value("division", 480);
    if (sequence->division <= 0) {
        return nullptr;
    }
    std::uint32_t carried = 0; // the time of events passed over
    for (const Json &e : source.value("events", Json::array())) {
        if (!e.is_object()) {
            return nullptr;
        }
        const auto found = e.find("delta");
        if (found != e.end() && !found->is_number_unsigned()) {
            return nullptr; // a delta is a count of ticks
        }
        const std::uint32_t delta = carried + (found != e.end() ? found->get<std::uint32_t>() : 0u);
        const std::string   kind = e.value("kind", "");
        // A channel message of one data byte (a program, say) or two.
        if (kind == "channel" && e.contains("data") && e["data"].is_array() && !e["data"].empty()) {
            sequence->events.push_back({.delta = delta,
                                        .kind = Event::Kind::Channel,
                                        .status = static_cast<std::uint8_t>(e.value("status", 0)),
                                        .data1 = static_cast<std::uint8_t>(e["data"][0].get<int>() & 0x7F),
                                        .data2 = static_cast<std::uint8_t>(e["data"].size() >= 2 ? e["data"][1].get<int>() & 0x7F : 0)});
        } else if (kind == "meta" && e.value("meta", 0) == 0x51 && e.contains("payload") && e["payload"].is_array() && e["payload"].size() >= 3) {
            const Json &p = e["payload"];
            sequence->events.push_back({.delta = delta,
                                        .kind = Event::Kind::Tempo,
                                        .tempo = static_cast<std::uint32_t>(p[0].get<int>() & 0xFF) << 16 | static_cast<std::uint32_t>(p[1].get<int>() & 0xFF) << 8 |
                                                 static_cast<std::uint32_t>(p[2].get<int>() & 0xFF)});
        } else if (kind == "end") {
            sequence->events.push_back({.delta = delta, .kind = Event::Kind::End});
        } else {
            carried = delta;
            continue;
        }
        carried = 0;
    }
    return sequence;
}

} // namespace

std::shared_ptr<const noktus::audio::Sequence> NoktusSequenceFromJson(std::string_view text, int song) {
    try {
        return NoktusSequenceFromJsonOrThrow(text, song);
    } catch (const nlohmann::json::exception &) {
        return nullptr;
    }
}

std::shared_ptr<const noktus::audio::Sequence> LoadNoktusSequence(const std::string &document, int song) {
    const std::optional<std::string> text = Text(document);
    return text ? NoktusSequenceFromJson(*text, song) : nullptr;
}

std::shared_ptr<audio::SqFile> LoadSequence(const std::string &document) {
    const std::optional<std::string> text = Text(document);
    return text ? audio::SqFile::CreateFromJson(*text) : nullptr;
}

std::optional<std::vector<SequenceVolume>> SequenceVolumes(std::string_view table) {
    const std::optional<Json> rows = TableRows(table, "sequence_volumes");
    if (!rows) {
        return std::nullopt;
    }
    std::vector<SequenceVolume> volumes;
    for (const Json &row : *rows) {
        if (row.is_object() && row.contains("name") && row["name"].is_string()) {
            volumes.push_back({.name = row["name"], .volume = Int(row, "volume")});
        }
    }
    return volumes;
}

std::optional<std::vector<EffectVolume>> EffectVolumes(std::string_view table) {
    const std::optional<Json> rows = TableRows(table, "effect_volumes");
    if (!rows) {
        return std::nullopt;
    }
    std::vector<EffectVolume> volumes;
    for (const Json &row : *rows) {
        if (row.is_object()) {
            volumes.push_back({.bank = Int(row, "bank"), .program = Int(row, "program"), .volume = Int(row, "volume")});
        }
    }
    return volumes;
}

} // namespace dc::sound
