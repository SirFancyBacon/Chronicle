#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <string>

// Retail's item tables as gameplay documents (docs/ITEM_FRAMEWORK.md, sections 6 and 7): ComItemInfo
// with each item's model name, ITEM_LIST, AttachList, WeaponList, the baits and PriceList, each extracted
// into a document and filled back from the winning one.

// The documents of the tables as compiled, by their paths under gameplay/. A pure function of the
// tables' original values, whatever has been loaded into them since.
std::map<std::string, nlohmann::json> ItemDocuments();

// Fills every table from its highest-priority valid document (datapacks::Load must have run), then
// registers every vanilla item with Items:: as the tables now hold it. A document that cannot be used
// leaves its table as it was.
void ItemsLoad();
