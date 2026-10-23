#pragma once

#include <filesystem>

// The gameplay documents (docs/ITEM_FRAMEWORK.md, sections 6 and 7): retail's tables extracted at
// start-up into documents served from memory as the base, datapacks/ beside the data folder lying over
// them, and the winning documents loaded back into the tables before anything reads them.

// Extracts the documents, loads the datapacks over them and fills the tables.
void GameplayLoad(const std::filesystem::path &data_root);

// Writes the extraction's documents under directory, pretty-printed, as a starting point for a pack.
// Returns false when one cannot be written.
bool GameplayExport(const std::filesystem::path &directory);
