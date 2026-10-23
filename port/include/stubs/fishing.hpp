#pragma once

// The item framework's bait document (port/src/item_host.cpp) reads and fills this unit's static
// bait table, declared extern here first so it has a global name the port can reach.
struct EsaInfo;
extern EsaInfo esa_info[13] asm("Fishing_esa_info");
