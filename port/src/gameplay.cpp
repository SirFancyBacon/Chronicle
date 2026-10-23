#include "gameplay.hpp"

#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "item_host.hpp"
#include "platform/datapacks.hpp"
#include "platform/memory_mount.hpp"
#include "platform/paths.hpp"

void GameplayLoad(const std::filesystem::path &data_root) {
    std::map<std::string, std::vector<std::uint8_t> > files;
    for (const auto &[path, document] : ItemDocuments()) {
        const std::string text = document.dump();
        files.emplace(path, std::vector<std::uint8_t>(text.begin(), text.end()));
    }
    datapacks::Load(MemoryMount(std::move(files), "extracted:"), data_root);
    ItemsLoad();
}

bool GameplayExport(const std::filesystem::path &directory) {
    for (const auto &[path, document] : ItemDocuments()) {
        const std::filesystem::path file = directory / path;
        std::error_code             error;
        std::filesystem::create_directories(file.parent_path(), error);
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        stream << document.dump(2) << '\n';
        if (error || !stream) {
            std::fprintf(stderr, "cannot write %s\n", PathsDisplay(file).c_str());
            return false;
        }
    }
    return true;
}
