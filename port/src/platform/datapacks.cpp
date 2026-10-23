#include "platform/datapacks.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace datapacks {
namespace {

// One enabled pack's mounted files.
struct Pack {
    std::filesystem::path             location;
    std::shared_ptr<const vfs::Mount> mount;
};

std::filesystem::path             data_location;
std::shared_ptr<const vfs::Mount> base;
// Enabled packs in increasing priority.
std::vector<Pack> packs;
// Folded relative names mapped to every pack file replacing them, highest priority first.
std::unordered_map<std::string, std::vector<vfs::Ref> > overrides;

std::string Display(const std::filesystem::path &path) {
    const auto utf8 = path.generic_u8string();
    return {utf8.begin(), utf8.end()};
}

const std::vector<vfs::Ref> *Overrides(std::string_view relative) {
    if (overrides.empty()) {
        return nullptr;
    }
    const auto found = overrides.find(vfs::FoldKey(relative));
    return found == overrides.end() ? nullptr : &found->second;
}

void Diagnostic(const std::filesystem::path &path, const std::string &message) {
    std::fprintf(stderr, "datapacks: %s: %s\n", Display(path).c_str(), message.c_str());
}

// Mounts one load_order entry, a folder. ChronicleTwo also takes the entry, or the entry plus .zip, as
// a ZIP archive; Chronicle reports such a pack and skips it until it reads ZIPs.
std::optional<Pack> OpenPack(const std::filesystem::path &directory, const std::filesystem::path &name) {
    std::error_code error;
    const auto      path = directory / name;
    auto            archive = path;
    archive += ".zip";
    if (std::filesystem::is_regular_file(path, error) || std::filesystem::is_regular_file(archive, error)) {
        Diagnostic(path, "ZIP packs are not read yet; skipped");
        return std::nullopt;
    }
    if (std::filesystem::is_directory(path, error)) {
        return Pack{.location = path, .mount = vfs::OpenDirectory(path, true)};
    }
    return std::nullopt;
}

} // namespace

void Clear() {
    packs.clear();
    overrides.clear();
    base.reset();
    data_location.clear();
}

void Load(std::shared_ptr<const vfs::Mount> base_mount, const std::filesystem::path &data_root) {
    Clear();
    base = std::move(base_mount);
    std::error_code error;
    data_location = std::filesystem::weakly_canonical(data_root, error);
    if (error) {
        Diagnostic(data_root, "cannot establish the data location; packs disabled");
        return;
    }
    const auto directory = data_location.parent_path() / "datapacks";
    const auto config = directory / "datapacks.json";
    if (!std::filesystem::exists(config, error) && !error) {
        return;
    }
    try {
        std::vector<std::uint8_t> text;
        if (error || !std::filesystem::is_regular_file(config) || !ReadFileBytes(config, text, 1024 * 1024)) {
            throw std::runtime_error("configuration is unreadable or exceeds 1 MiB");
        }
        const auto root = nlohmann::json::parse(text);
        if (!root.is_object() || !root.contains("schema_version") || !root["schema_version"].is_number_integer() ||
            root["schema_version"] != 1 || !root.contains("load_order") || !root["load_order"].is_array()) {
            throw std::runtime_error("expected schema_version 1 and a load_order array");
        }
        std::vector<Pack> configured;
        for (const auto &item : root["load_order"]) {
            if (!item.is_string()) {
                throw std::runtime_error("load_order entries must be pack names");
            }
            const auto                  folder = item.get<std::string>();
            const std::filesystem::path name(std::u8string(folder.begin(), folder.end()));
            if (!vfs::SafeRelative(folder) || std::distance(name.begin(), name.end()) != 1) {
                throw std::runtime_error("load_order contains an unsafe pack name");
            }
            auto pack = OpenPack(directory, name);
            if (!pack) {
                Diagnostic(directory / name, "pack is missing; skipped");
                continue;
            }
            configured.push_back(std::move(*pack));
        }
        // One merged index resolves load_order once: each name maps to its replacements, highest priority first.
        std::unordered_map<std::string, std::vector<vfs::Ref> > merged;
        for (auto &pack : std::views::reverse(configured)) {
            const auto count = pack.mount->Count();
            for (std::size_t index = 0; index < count; ++index) {
                merged[vfs::FoldKey(pack.mount->Name(index))].emplace_back(pack.mount, index);
            }
        }
        packs = std::move(configured);
        overrides = std::move(merged);
    } catch (const std::exception &exception) {
        packs.clear();
        overrides.clear();
        Diagnostic(config, std::string(exception.what()) + "; packs disabled");
    }
}

const std::shared_ptr<const vfs::Mount> &Base() {
    return base;
}

const std::filesystem::path &Root() {
    return data_location;
}

std::optional<vfs::Ref> Resolve(const std::filesystem::path &relative) {
    const auto name = vfs::RelativeName(relative);
    if (!name) {
        return std::nullopt;
    }
    const auto *replacements = Overrides(*name);
    if (!replacements) {
        return std::nullopt;
    }
    return replacements->front();
}

std::vector<vfs::Ref> Candidates(const std::filesystem::path &relative) {
    const auto name = vfs::RelativeName(relative);
    if (!name) {
        return {};
    }
    std::vector<vfs::Ref> candidates;
    if (const auto *replacements = Overrides(*name)) {
        candidates = *replacements;
    }
    if (auto original = vfs::Find(base, *name)) {
        candidates.push_back(std::move(original));
    }
    return candidates;
}

std::vector<vfs::Ref> GameplayCandidates(const std::filesystem::path &relative) {
    const auto name = vfs::RelativeName(relative);
    if (!name) {
        return {};
    }
    const std::string     logical = name->starts_with("gameplay/") ? name->substr(9) : *name;
    const std::string     physical = "gameplay/" + logical;
    std::vector<vfs::Ref> candidates;
    const auto            append = [&](const std::shared_ptr<const vfs::Mount> &mount) {
        if (auto file = vfs::Find(mount, physical)) {
            candidates.push_back(std::move(file));
        } else if (auto file = vfs::Find(mount, logical)) {
            candidates.push_back(std::move(file));
        }
    };
    for (const auto &pack : std::views::reverse(packs)) {
        append(pack.mount);
    }
    append(base);
    return candidates;
}

std::optional<vfs::Ref> Asset(const std::filesystem::path &relative) {
    auto candidates = Candidates(relative);
    if (candidates.empty()) {
        return std::nullopt;
    }
    return candidates.front();
}

bool IsReplacement(const vfs::Ref &file) {
    return file && file.mount() != base;
}

std::vector<std::filesystem::path> DirectoryMembers(const std::filesystem::path &relative) {
    const auto name = vfs::RelativeName(relative);
    if (!name) {
        return {};
    }
    std::unordered_map<std::string, std::string> members;
    auto collect = [&](const std::shared_ptr<const vfs::Mount> &mount, bool is_pack) {
        if (!mount) {
            return;
        }
        for (const auto &member : mount->List(*name)) {
            const auto path = *name + '/' + member.name;
            if (!member.directory || is_pack || mount->Find(path + "/pack.json")) {
                members.insert_or_assign(vfs::FoldKey(member.name), path);
            }
        }
    };
    collect(base, false);
    for (const auto &pack : packs) {
        collect(pack.mount, true);
    }
    std::vector<std::pair<std::string, std::filesystem::path> > sorted;
    sorted.reserve(members.size());
    for (const auto &[key, path] : members) {
        sorted.emplace_back(vfs::FoldKey(path), std::filesystem::path(std::u8string(path.begin(), path.end())));
    }
    std::ranges::sort(sorted, {}, &std::pair<std::string, std::filesystem::path>::first);
    std::vector<std::filesystem::path> result;
    result.reserve(sorted.size());
    for (auto &[key, path] : sorted) {
        result.push_back(std::move(path));
    }
    return result;
}

bool ReadFileBytes(const std::filesystem::path &path, std::vector<std::uint8_t> &bytes, std::uint64_t max_bytes) {
    bytes.clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const auto end = static_cast<std::streamoff>(file.tellg());
    if (end < 0 || std::cmp_greater(end, max_bytes) ||
        static_cast<std::uint64_t>(end) > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    file.seekg(0);
    bytes.resize(static_cast<std::size_t>(end));
    if (!bytes.empty() && !file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        bytes.clear();
        return false;
    }
    return true;
}

} // namespace datapacks
