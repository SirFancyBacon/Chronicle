#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "platform/vfs.hpp"

// Files made in memory, served as a read-only mount: the gameplay documents the port extracts from
// retail's tables at start-up form the base the datapacks lie over (docs/ITEM_FRAMEWORK.md, section 6).
// Names are '/'-separated relative paths, looked up ignoring ASCII case as every mount's are.
std::shared_ptr<const vfs::Mount> MemoryMount(std::map<std::string, std::vector<std::uint8_t> > files,
                                              std::string display_prefix);
