#include "platform/memory_mount.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace {

class Memory final : public vfs::Mount {
public:
    Memory(std::map<std::string, std::vector<std::uint8_t> > files, std::string prefix) : prefix_(std::move(prefix)) {
        for (auto &[name, bytes] : files) {
            index_.emplace(vfs::FoldKey(name), entries_.size());
            entries_.push_back({name, std::move(bytes)});
        }
    }

    std::size_t Count() const override { return entries_.size(); }

    std::string Name(std::size_t index) const override { return entries_[index].name; }

    std::string Display(std::size_t index) const override { return prefix_ + entries_[index].name; }

    std::optional<std::size_t> Find(std::string_view relative) const override {
        const auto found = index_.find(vfs::FoldKey(relative));
        return found == index_.end() ? std::nullopt : std::optional(found->second);
    }

    std::optional<std::uint64_t> Size(std::size_t index) const override { return entries_[index].bytes.size(); }

    bool Read(std::size_t index, std::vector<std::uint8_t> &bytes, std::uint64_t max_bytes) const override {
        if (entries_[index].bytes.size() > max_bytes) {
            return false;
        }
        bytes = entries_[index].bytes;
        return true;
    }

    bool ReadInto(std::size_t index, void *buffer, std::size_t size) const override {
        if (size != entries_[index].bytes.size()) {
            return false;
        }
        std::memcpy(buffer, entries_[index].bytes.data(), size);
        return true;
    }

    std::string Stamp(std::size_t index) const override {
        const auto &bytes = entries_[index].bytes;
        const auto  hash = std::hash<std::string_view>{}(
            std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
        return "memory:" + std::to_string(bytes.size()) + ":" + std::to_string(hash);
    }

    std::vector<vfs::Member> List(std::string_view directory) const override {
        const std::string prefix = directory.empty() ? std::string() : vfs::FoldKey(directory) + '/';
        std::set<std::string>    seen;
        std::vector<vfs::Member> members;
        for (const auto &entry : entries_) {
            const std::string key = vfs::FoldKey(entry.name);
            if (!key.starts_with(prefix)) {
                continue;
            }
            const std::string_view rest = std::string_view(entry.name).substr(prefix.size());
            const auto             slash = rest.find('/');
            const std::string      name(rest.substr(0, slash));
            if (seen.insert(vfs::FoldKey(name)).second) {
                members.push_back({name, slash != std::string_view::npos});
            }
        }
        return members;
    }

private:
    struct Entry {
        std::string               name;
        std::vector<std::uint8_t> bytes;
    };

    std::string                                  prefix_;
    std::vector<Entry>                           entries_;
    std::unordered_map<std::string, std::size_t> index_;
};

} // namespace

std::shared_ptr<const vfs::Mount> MemoryMount(std::map<std::string, std::vector<std::uint8_t> > files,
                                              std::string display_prefix) {
    return std::make_shared<const Memory>(std::move(files), std::move(display_prefix));
}
