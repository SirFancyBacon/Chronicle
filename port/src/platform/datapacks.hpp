#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "platform/vfs.hpp"

// Datapacks: folders in datapacks/ beside the data folder whose files replace the data's at the same
// paths. datapacks/datapacks.json lists the active ones, {"schema_version": 1, "load_order": [...]},
// later packs winning; a path a pack lacks falls through to earlier packs and then to the base. Brought
// across from ChronicleTwo's port with its names and behaviour (docs/ITEM_FRAMEWORK.md, section 7);
// ChronicleTwo also mounts ZIP packs and decodes their textures, which Chronicle leaves out for now.
namespace datapacks {

// Mounts base as the original data and loads datapacks/datapacks.json beside data_root. A bad
// configuration disables every pack, with a diagnostic; a listed pack that is missing is skipped.
void Load(std::shared_ptr<const vfs::Mount> base, const std::filesystem::path &data_root);

// Clears the base, the packs and the override index.
void Clear();

// The mount holding the original data, or null before Load.
const std::shared_ptr<const vfs::Mount> &Base();

// The data location given to Load.
const std::filesystem::path &Root();

// The highest-priority pack file replacing a relative data path, ignoring ASCII case.
std::optional<vfs::Ref> Resolve(const std::filesystem::path &relative);

// Every pack file replacing a relative path, highest priority first, then the original.
std::vector<vfs::Ref> Candidates(const std::filesystem::path &relative);

// Candidates for a gameplay document under gameplay/, accepting an unprefixed path in each mount.
std::vector<vfs::Ref> GameplayCandidates(const std::filesystem::path &relative);

// The direct files and child directories of a relative directory across the base and every pack.
std::vector<std::filesystem::path> DirectoryMembers(const std::filesystem::path &relative);

// The highest-priority replacement, or else the original.
std::optional<vfs::Ref> Asset(const std::filesystem::path &relative);

// Whether a file comes from a pack rather than the base.
bool IsReplacement(const vfs::Ref &file);

// Reads a whole host file; false when it is missing, unreadable or larger than max_bytes.
bool ReadFileBytes(const std::filesystem::path &path, std::vector<std::uint8_t> &bytes,
                   std::uint64_t max_bytes = 256ull * 1024 * 1024);

} // namespace datapacks
