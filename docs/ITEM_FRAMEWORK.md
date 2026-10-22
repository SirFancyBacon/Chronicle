# Item Framework (`Items::` Namespace)

**Status: proposal.** Nothing here is built yet. This document is for agreeing on the design before
the code: what an item is to the port, how items are defined and changed, and how the save stays
vanilla. The fishing and weapon frameworks are to be built on it, and the quest framework is to be
extended by them (section 12).

Item data becomes documents in the format ChronicleTwo already uses for its gameplay data: the
vanilla ones generated at start-up from retail's own tables, and datapacks able to replace them.
The port loads the winning documents into retail's tables, and a registry over them tells systems
what an item can do, through the traits its definition carries, instead of each system keeping its
own table or number range.

Chronicle leans on Kazarov, the build system and runtime it shares with ChronicleTwo, and mirrors
ChronicleTwo where Kazarov doesn't yet cover something. This is not a mod system: there are no mod
packages or manifests, only ChronicleTwo's datapacks (section 8).

---

## 1. Goals

- **Parity.** With no datapacks, the game behaves exactly as retail, and a save is byte for byte
  what retail writes (section 9). Every vanilla item keeps its own code paths.
- **Item data is documents, as ChronicleTwo's is,** so any item, vanilla ones included, can be
  changed without recompiling, and the runtime extraction can later move into Kazarov unchanged
  (sections 6 and 7).
- **Considerate of mods.** Nothing in the definition format or the save assumes a single author, so
  definitions from different sources can sit together with a defined result (section 8).
- **Copies can carry their own state,** as retail's weapons and attachments already do, for custom
  weapons and attachments (section 5).
- **A save survives a missing definition.** A custom item whose definition is gone becomes its
  vanilla fallback.

Not in the first version: several copies in one bag slot (section 10), models other than retail's
MDS (section 13), and weapon behaviour, which is the weapon framework's (section 12). The inventory
screens move to the UI framework with custom items (section 11).

---

## 2. Vanilla today

What the design has to fit. Counts are of `ps2/src` as of this proposal.

**Identity.** An item is a 16-bit number. `ComItemInfo[296]` (`itemdata.cpp`) covers 81 to 376
and maps each to a kind (attachment, item or weapon), an index into that kind's table, an icon and
a name message. Weapons are 257 to 375. 1 to 80 is a second numbering of the weapons that the menus
use; `TransWepNo` and `TransWepNoNewToOld` (`menu_save.cpp`) convert between the two. The weapons
come in six blocks, one per character, and retail tells a weapon's character from the block its
number is in (`btmisc.cpp`, `btsysscript.cpp`).

**Properties,** each kind in its own table:

| Table | Holds |
|---|---|
| `ITEM_LIST[175]` (`ITEM_DATA`) | sort group, use flags (HP, water, status), kind flags (consumable, throwable, random), volume, quick-slot `stack_kind` |
| `AttachList[50]` (`ATTACH_DATA`) | an attachment's stats, elements and monster effectiveness |
| `WeaponList[120]` (`WEAPON_DATA`) | a weapon's base stats, durability and holes |
| `esa_info[13]` (`fishing.cpp`, `static`) | the baits and how far fish notice each |
| `ITEM_NAME_TBL_NEW` (`btitem.cpp`) | the model file each item is drawn with |

**Where copies live,** all fixed-size parts of the save (`userstatus.hpp`, `stockitem.hpp`):

| Place | Per copy |
|---|---|
| `ITEM_PACK::item[103]` | item number and `item_vol`, one item per slot |
| `ITEM_PACK::quick_item_slot[3]` | item, `quick_item_qty`, `active_item_vol`: the only place copies stack |
| `consumable_items[43]` | the attachments the player carries (`DNG_CONSUMABLE`) |
| `chara_weapons[6][11]` (`WEAPON_HAVE`, 0xF8 bytes) | level, stats, durability, experience, elements, synthesis count, and six `ATTACH_LIST` copies (0x20 bytes each) |
| `CSaveData::stock_item` (`CStockItem`) | storage: 60 items with their `item_vol`, 30 weapons (`WEAPON_HAVE`, holes included) and 30 attachments (`ATTACH_LIST`) |

**How code asks about an item.**

- Lookups: `GetCommonItemInfo` (33 call sites), `GetItemTypeInfo`, `GetItemData` (21),
  `GetAttachData`, `GetWeaponDataInfo`.
- Tables read directly, past those lookups: 14 places, in `itembombeffect.cpp`, `itemdata.cpp` and
  `mainitemmodel.cpp`. Every price goes through `GetItemMoney` (`shop.cpp`), a `static` function
  that reads `PriceList` by number.
- Number ranges: 276 comparisons against `ITEM_*_START` and `ITEM_*_END` in 20 files decide an
  item's kind from its number (`shop.cpp` 96, `battlemenu.cpp` 61, `menu_draw.cpp` 35,
  `dngstatusdata.cpp` 19, ...). 14 of them are already in code `port/src` replaces.
- What using an item does is decided in `menu_misc.cpp` (from the menu), `mainitemmodel.cpp` (the
  item held up) and `itembombeffect.cpp` (thrown).
- Model paths come from one function, `BtGetItemNamePath` (`btmisc.cpp`), with about eight callers.
- Icons: `icon_index` is read in four places, and the `itemicon` texture is drawn at about eight,
  three of them already in `port/src`.

The range comparisons are why a brand-new item number is the expensive part (section 5).

---

## 3. Layers

As in the quest framework, dependencies flow downward only.

1. **`Items::` runtime** (`port/include/item_framework.hpp`, `port/src/item_framework.cpp`). The
   registry, definitions, traits, document parsing, handles and the items chunk of the save's
   extension file. No PS2 headers.
2. **Host** (`port/src/item_host.cpp`). The runtime extraction and the loaders (section 6), the
   replaced lookups, and the save hooks. The only tier that includes game headers.
3. **Content.** The documents (section 7), and `port/src/items/` for any item the port itself adds
   in C++.

The fishing, weapon and quest frameworks sit beside the content tier and use the runtime's API.

---

## 4. An item

An item is a `Def`: what every item has, plus the traits that say what it can do.

```cpp
Items::Def GoldenLure() {
    return Items::Builder("chronicle:golden_lure")
        .Fallback("vanilla:prickly")            // what a save without this item gets instead
        .Sort(Items::Sort::Goods)
        .Icon("icons/golden_lure.png")
        .Model("vanilla:prickly")               // drawn with Prickly's model
        .Price(400)
        .With(Items::Bait{.radius = 40.0f, .likes = {"vanilla:baron_garayan"}})
        .With(Items::QuickSlot{.stack = Items::Stack::Multi, .max = 9})
        .Build();
}
```

Name and description are text keys (`item.chronicle.golden_lure.name`, `.description`) looked up in
`port/lang/<language>.json`, as quest text is. A vanilla item's `Def` is assembled from its records
in the documents (section 7); the builder is for items the port itself adds in C++.

**Traits** are the point of the design. A system asks whether an item has the trait it cares about,
rather than what type it is, so one item can be goods, bait and a quest item at once, and a new
trait can be added without touching the items that don't use it.

| Trait | Says | Vanilla items get it from |
|---|---|---|
| `Consumable` | heals HP, restores water, grants a status | `ITEM_DATA::use_flags`, `kind_flags` |
| `Throwable` | can be thrown, and its effect | `ITEMKINDF_THROWABLE`, `itembombeffect.cpp` |
| `QuickSlot` | may go in a quick-use slot, stacks or not, how many | `ITEM_DATA::stack_kind` |
| `Attachment` | stats, elements, monster effectiveness | `AttachList` |
| `Weapon` | base stats, durability, holes; behaviour is the weapon framework's | weapon data |
| `Bait` | how far fish notice it, which fish like it | `esa_info` |
| `Fish` | a fish the player keeps as an item | the fish items (`ITEM_SORT_FISH`) |
| `Sellable` | the item's buy and sell price | `PriceList` |
| `KeyItem`, `QuestItem` | cannot be sold or dropped | `ITEM_SORT_KEY_ITEM`, `ITEM_SORT_QUEST` |

`Build` rejects a definition with no fallback when its kind needs one (anything a save can hold), a
fallback that isn't a vanilla item of the same kind (for a weapon, one the same character can hold;
section 9), and traits that cannot go together (a `Weapon` that is also `Consumable`). A custom
weapon belongs to its fallback's character; the `Weapon` trait records no character of its own.

Items the port defines in C++ are written into the base layer at start-up, beside the extraction
(section 6), as documents of their own (section 7). A datapack overrides one as it overrides a
vanilla table, so changing a port item needs no recompile.

---

## 5. Numbers and copies

An item has a string ID in data and a number at run time, because retail's code and saves can only
pass numbers around.

**IDs** are `namespace:name`: `vanilla:` for retail's items (`vanilla:prickly`), `chronicle:` for
the port's own. A vanilla row in a document keeps its retail `item_no`; a custom item (phase 3) is a
document of its own, named by its `id` (section 7). Saves only ever name custom items by ID, so their numbers can be handed out
afresh each run without breaking anything.

**Numbers:**

| Range | Is |
|---|---|
| 1 to 80 | retail's menu numbering of the weapons (unchanged) |
| 81 to 376 | retail's items (unchanged, the same numbers every run); 377 is `ITEM_WEAPON_END` |
| 0x200 to 0x0FFF | custom definitions, given out in the order they load, and orphans without state (section 9) |
| 0x1000 to 0x7FFF | handles: one custom copy each (below) |

**A copy with state of its own gets its own number, its handle.** Retail moves copies around in
many places: sorting, equipping, synthesising, storing, shops. Every one of them carries the item
number along. A copy whose number is its handle therefore keeps its identity through all of that
code without any of it being replaced. The registry maps the handle to the copy's definition and
the copy's extra state. The fields retail already has stay in the real struct: a custom weapon's
level, stats and attachments are in its `WEAPON_HAVE` like any other weapon's, so build-up and
synthesis work on it unchanged.

**What a new number costs.** The 276 range comparisons decide an item's kind from its number, and
any number above 377 falls on the weapon side of the 69 `>= ITEM_WEAPON_START` tests:

- A custom **weapon** above 377 already passes almost all of them; the two `ITEM_WEAPON_END` tests
  need replacing. Its number is in none of the six character blocks, so the tests that pick a
  weapon's character by block ask the registry instead, which answers with its fallback's
  character. `TransWepNo` and `TransWepNoNewToOld` return any number outside their blocks
  unchanged, so a handle crosses the menus' numbering intact. An event script that asks for the
  equipped weapon (`editloop3.cpp`) gets the handle, which matches none of the weapons scripts test
  for.
- A custom **attachment or dungeon item** fails the tests bounded on both sides (`ITEM_ATTACH_START`
  to `ITEM_ATTACH_END`, `ITEM_DUNGEON_START` to `ITEM_DUNGEON_END`), whose ranges are already full.
  Every function with such a test has to ask the registry instead (`Items::KindOf`), which means
  replacing it in `port/src`.

That is why custom numbers come in the third phase (section 14). The first phase changes vanilla
items only, which needs no new numbers at all. Most of the comparisons are in the inventory screens,
which are rebuilt on the UI framework rather than patched (section 11); the rest, outside any
screen (`dngstatusdata.cpp`'s pickup and pack code, `weaponlevelup.cpp`, ...), are replaced one
function at a time.

**Only copies with state of their own get handles.** Items don't roll random stats, so most custom
items, such as a potion or a bait, carry nothing per copy: every copy is just the definition's
number, as every Prickly is 199. Handles are for weapons and attachments, whose copies retail
already keeps one by one.

**Two identities: one kept in the save, one at run time.** A copy's identity in the save is a 64-bit
instance ID, given once and never reused. Its handle is only the 16-bit alias it travels under while
the game runs: assigned when the save loads, freed when the copy is gone, and reused freely, since
nothing kept depends on it. The handle has to be 16 bits, because it lives where retail keeps item
numbers (`ITEM_PACK::item`, `WEAPON_HAVE::item_no`, `ATTACH_LIST::item_no`, the quick slots), which
is what lets retail's code carry it without change. Wider numbers would mean replacing those
structures, and with them vanilla saves. The 16 bits only limit the copies alive at once, and the
save's fixed arrays already limit those to about 900 (103 pack slots, three quick slots, 43 carried
attachments, 66 carried and 30 stored weapons with six holes each, 60 stored items and 30 stored
attachments), far below the 28,000 handles. This is the split games built
on data generally make (Bethesda's engines, for one, keep an item's definition apart from the few
copies that carry extra data), without their growth problem: here only the permanent IDs persist.

**A handle's life.** A handle is given out when a custom copy is made (a pickup, a purchase, a
synthesis) or a save is loaded, and freed when no save structure holds it any more. The host checks
this when saving: a handle found in no slot, weapon, hole or storage space is dropped, and its copy
with it.

---

## 6. Vanilla items: extracted at run time

ChronicleTwo's item data is on its disc, in configuration scripts that Kazarov's extractor turns into
documents. Dark Cloud's is compiled into the game (section 2), so Chronicle extracts it at run time
instead: from retail's tables in memory, into the same kind of documents.

**The extraction is a pure function** from retail's tables to documents: no game state, no files
written. At start-up, before anything reads an item, the host runs it on the tables' original
values and serves the result as the base of the file tree, in memory (a `vfs::Mount`, as the
retail icon converter of #208 serves a converted icon). Datapacks lie over it (section 7).

**The loaders fill retail's tables** from the winning documents, as ChronicleTwo's loaders fill its
game's structures from its documents. Retail's code then reads the tables as it always has, with
nothing replaced. With no datapack in play, the documents are the extraction's own, and the tables
come out exactly as compiled; a test proves it (section 15), as ChronicleTwo's extractor proves each
document reproduces the script it came from.

### What a document reaches, and what it doesn't

Filling retail's tables is what spares the port from replacing the code that reads them. It only
reaches what retail actually keeps in a table, though. Item data falls into four groups, and a
document only gets a field when that field is in the first two, so no field silently does nothing.

- **Read from a table whenever it is needed: takes effect with nothing replaced.** Item properties
  (`ITEM_LIST`: use and kind flags, volume, `stack_kind`), attachment stats (`AttachList`), weapon
  base stats (`WeaponList`), icons and name messages (`ComItemInfo`), and prices (`PriceList[296]`,
  `shop.cpp`).
- **Copied into a copy when it is made: takes effect for new copies.** A weapon's stats and
  durability are seeded from `WeaponList` when it is obtained and when it levels up
  (`weaponlevelup.cpp`), and then live in its `WEAPON_HAVE` in the save. A changed `weapons` table
  therefore changes the weapons obtained after it, not the ones a save already holds, as a save's
  weapons are its own in retail too.
- **Written into code, not a table: unreachable until that code is replaced.** 164 `case ITEM_...`
  labels in seven files decide per item what happens (`battlemenu.cpp` 98, `mainitemmodel.cpp` 33,
  `itembombeffect.cpp` 10, `randomitem.cpp` 9, `dun/gameloop.cpp` 9, `menu_misc.cpp` 3,
  `weapon_buildup.cpp` 2), 118 comparisons against particular items in 16 files do the same, the
  thirteen baits are a list inside `_GOTO_CHANGE_ESA` (`editloop3.cpp`), and the 276 range
  comparisons decide an item's kind. Each of these comes into a document only when a framework
  replaces the function that holds it: the bait list with fishing (phase 2), the range comparisons
  with custom items (phase 3), and per-item behaviour through the trait callbacks below, one
  function at a time and only where something needs it. The functions are replaced as the rest of
  `port/src` replaces retail's, with `PORT_OVERRIDE`, or for a `static` one such as a script command,
  through its unit's stubs header (as `PortWorldMapCommand` replaces `_WORLD_MAP`).
- **Fixed sizes.** The tables are arrays of fixed length (`ITEM_LIST[175]`, `PriceList[296]`, ...).
  A row for an item number retail doesn't have is refused, as ChronicleTwo's loaders skip entries a
  table can't hold; custom items come in documents of their own (section 7, phase 3).
- **Fields that shape a save are fixed.** An item's kind and table index (`ComItemInfo`) and a
  weapon's number of holes decide where a save's copies can sit, so they come from the extraction
  only, and a document that changes one is refused. Documents tune values; they don't reshape what
  a save holds (section 9).

The loaders run once at start-up, before the title screen and so before anything reads an item;
changing a pack takes a restart, as it does in ChronicleTwo.

**Moving into Kazarov later.** The extraction is kept separate from everything that reads its
output, so it can move without anything else changing. Retail's tables are also in the game's
executable on the disc, at addresses the decompilation already knows, so Kazarov's extractor could
read them there and publish the same documents into the game data, as it does ChronicleTwo's. The
in-memory mount would then go, and the loaders and registry would read the same paths from the data
instead. Until then Chronicle keeps `dcdata` and its extraction as they are.

**The lookups.** `GetCommonItemInfo`, `GetItemTypeInfo`, `GetItemData`, `GetAttachData` and
`GetWeaponDataInfo` are replaced in `port/src` to answer for custom numbers and handles from the
registry (phase 3), and as retail does for everything else. `GetItemMoney` would read past
`PriceList` for a custom number, and is `static`, so its callers, all in the shop screens, are
replaced instead; the shops' rebuild (section 11) does that.

**Behaviour.** A trait may carry callbacks (`OnUse`, `OnThrow`, `CanUseHere`, `OnBaitAttach`),
called from the three places retail decides what using an item does, and only for items that set
them. A vanilla item without a callback goes through retail's code exactly as before.

---

## 7. Documents

The documents mirror ChronicleTwo's (its `docs/PORT_CONFIG_FORMAT.md` and `docs/NATIVE_CONFIGS.md`),
so a reader of one port's data can read the other's.

**One document per table, not per item,** as ChronicleTwo's `item_common`, `items`, `attachments`,
`weapons` and `fish` documents are each one retail table. Each is an object with `schema_version`,
its `kind`, and the table's rows under their retail field names, typed as the game reads them:

| Path | `kind` | From |
|---|---|---|
| `gameplay/items/item_common.json` | `item_common` | `ComItemInfo`, with each item's model name from `ITEM_NAME_TBL_NEW` |
| `gameplay/items/items.json` | `items` | `ITEM_LIST` |
| `gameplay/items/attachments.json` | `attachments` | `AttachList` |
| `gameplay/items/weapons.json` | `weapons` | `WeaponList` |
| `gameplay/items/bait.json` | `bait` | `esa_info` (`static` in `fishing.cpp`, given a link name in `port/include/stubs/fishing.hpp`) |
| `gameplay/items/prices.json` | `prices` | `PriceList` (`shop.cpp`) |

```json
{
  "schema_version": 2,
  "kind": "items",
  "items": [
    {
      "item_no": 199,
      "sort_key": 6,
      "use_flags": 0,
      "kind_flags": 0,
      "vol": 1,
      "vol_range": 0,
      "stack_kind": 2
    }
  ]
}
```

Each document is one of retail's tables, as each of ChronicleTwo's is one of its retail files. That
is why prices are a document of their own here: Dark Cloud's `PriceList` is a table apart from the
shops' stock, while Dark Cloud 2's prices share a document with its shop lists only because its
`shoplst.cfg` held both. Either way a price is one row per item.

`schema_version` is 2 because that is the version of ChronicleTwo's native documents, which these
follow; one loader can then read both ports' documents without a special case. Further tables
(shop stock, fish) are added the same way as their frameworks need them.

**Overridden through datapacks,** brought across from ChronicleTwo (its `docs/PC.md`, "Datapacks and
replacement textures"). A datapack is a ZIP or a folder in `datapacks/` beside the game data, holding
files at the paths they replace; `datapacks/datapacks.json` lists the active packs in `load_order`,
later packs winning, and a path a pack doesn't hold falls through to earlier packs and then to the
base. A pack holding `gameplay/items/items.json` replaces the whole `items` table, as a ChronicleTwo
pack replaces a whole document.

**Custom items are a document each** (phase 3). Vanilla tables are replaced whole, but custom items
are added, and two packs that each add items must both be heard; were custom items rows of
`items.json`, the later pack would silently remove the earlier one's. So each is its own document,
`gameplay/items/custom/<namespace>.<name>.json` (`kind` `item`; a file name can't hold the ID's
colon), with everything the item has across the tables: its common fields, its kind's properties,
its price and its traits. The loader lists the folder across the base and every active pack with
the index's `DirectoryMembers`, which ChronicleTwo's index already has: every pack's files are read,
and a later pack's file at the same path replaces an earlier one's, so one item is still overridden
whole.

**A starting point.** `darkcloud --export-gameplay <directory>` writes the extraction's documents to
disk, pretty-printed, so changing one field of one item means copying the table's document into a
pack and editing one line.

**A bad candidate falls back.** The candidates for a path are tried from the highest priority down.
The first whose `schema_version` is supported, whose `kind` is the one the loader reads and whose
fields parse is used. Parsing includes range: a value retail's field can't hold (a price below zero,
a count past the field's width, a flag retail doesn't define) fails the candidate, so a pack can't
put into a table what retail's code was never written to read. One that fails prints one line, `config: <path>: <reason>`, as ChronicleTwo's
loaders do, and the next is tried, down to the extraction. A broken file never empties a table, and
nothing in a pack can stop the game from starting.

**Where the datapack code comes from.** ChronicleTwo's datapack index is in its own port
(`port/src/platform/datapacks.cpp`, 953 lines, with `datapacks_test.cpp`), built on Kazarov's `vfs`.
It does three things, and Chronicle brings across the one items need:

- **The index:** reading `datapacks.json`, mounting the packs in `load_order`, and resolving a path
  to its winning file and the candidates behind it (`Load`, `Resolve`, `Candidates`,
  `GameplayCandidates`, `DirectoryMembers`). This comes across unchanged in behaviour and names,
  beside Chronicle's `dcdata` layout (`datapacks/` beside the data folder), so it can move into
  Kazarov once both ports want to share it.
- **ZIP packs,** through ChronicleTwo's `platform/archive_mount` (not in Kazarov) and Zstandard for
  compressed entries. Folder packs, which ChronicleTwo accepts with the same layout, need neither, so
  Chronicle starts with folders; ZIPs follow when someone needs them, bringing `archive_mount` and a
  Zstandard dependency with them.
- **Texture replacement:** about forty of the 953 lines, and the only user of Kazarov's Rust bridge
  (one PNG decode). Chronicle has its own texture replacement (`docs/PC.md`), so this part stays in
  ChronicleTwo.

ChronicleTwo pins Kazarov's current `master` (`96c2d95f`), three commits ahead of Chronicle's pin
(`28281ceb`); the `vfs` calls the index makes are checked against Chronicle's pin when it comes
across.

---

## 8. Considerate of mods

There is no mod system, and building one is not part of this work: datapacks already let anyone
replace files, and items need nothing more. But data will come from people with different tools and
habits, against different versions of the port, so the format and the save are built not to assume
one author, following ChronicleTwo's rules where it has them.

- **The order is decided, never accidental.** Packs apply in `datapacks.json`'s `load_order`, so a
  given set of packs gives the same result on every machine.
- **Whole-document overrides are predictable.** Where two packs hold the same table, the later one
  wins entirely, as with every file in ChronicleTwo's datapacks. Two packs changing different items
  of one table therefore don't combine; merging them is a rule every Kazarov document would want, so
  it belongs in Kazarov if it is ever wanted, not in items alone. Custom items don't meet this rule:
  each is a document of its own (section 7), so packs adding different items combine, and two packs
  holding the same item resolve as any document does.
- **Unknown fields and traits are kept and reported, not fatal.** A document written for a newer
  version of the port then loads as much as this one understands.
- **Versions.** A newer `schema_version` than the port supports refuses that candidate, with a
  message saying which version it needs, and the next candidate is used.
- **Fallbacks keep saves playable** when a custom item's definition is gone (section 9).

---

## 9. Saving

**The vanilla bytes stay vanilla.** With no custom items in it, a save is byte for byte what retail
writes, and a test checks it. Otherwise the host moves every custom item out of the save structures
before writing, leaving what retail would hold there, and puts them back after loading. What it moved
out goes in the save's extension file.

### The extension file

Custom items are kept in one file per save slot, `saves/N/chronicle.dat`, beside the `save.dat` it
belongs to (`docs/PC.md` keeps the slot's folder open for more files). Items never use a loose JSON
file beside the save as quests do today: nothing would tie such a file to its `save.dat`, so copying
`save.dat` alone, restoring an older backup or dropping in a card export would apply the old file to
the wrong save. Phase 3 brings the extension file with custom items.

- **Chunks:** a container of chunks (tag, version, length, CRC), one per framework: quests, items,
  fishing and so on, each registering its chunk's writer and reader.
- **Bound to its `save.dat`** by a hash of the exact `save.dat` bytes each set of records belongs to
  (below). A set whose hash matches no `save.dat` on disk is treated as stale.
- **`save.dat` never changes.** It stays retail's image, so card exports load as they are and the
  byte-for-byte test holds; the tie lives entirely in the second file.
- **Binary, with a way to read it:** a small versioned binary format, and a dump command (in `dcdata`)
  that prints a slot's chunks as text for debugging.
- **Migration:** an existing `quests.json` is read once, folded into the container and removed.

The items chunk, as the dump command prints it:

```json
{
  "version": 1,
  "next_instance": "0x00000000000004d3",
  "copies": [
    { "item": "lures:golden_lure", "at": "pack:17", "fallback": "vanilla:prickly" },
    {
      "instance": "0x00000000000004d2",
      "item": "forge:storm_blade",
      "at": "weapon:0:3",
      "fallback": "vanilla:dagger",
      "state_version": 1,
      "state": { "charge": 12 }
    }
  ]
}
```

- `at` is where the copy sits: `pack:<slot>`, `quick:<slot>`, `attach:<slot>` (the attachments
  carried), `weapon:<character>:<slot>` and `hole:<character>:<slot>:<hole>`, and in storage
  `stock:<slot>`, `stock_weapon:<slot>`, `stock_hole:<slot>:<hole>` and `stock_attach:<slot>`. The
  vanilla struct at that place holds the copy's fallback. A quick slot's stack is one record.
- A **weapon or attachment**'s vanilla fields (level, stats, attachments) stay in its real
  `WEAPON_HAVE` or `ATTACH_LIST` in `save.dat`. With its definition gone, the copy becomes its
  fallback with them intact, so a character never loses a weapon or its progress.
- `instance` is the copy's permanent ID (section 5), for copies with state of their own;
  `next_instance` is where the save's counter stands, so an ID is never given twice in one save.
- `state` is only what retail has no field for, with the version of the definition that wrote it.
- Deleting a save deletes its extension file.

### Missing definitions

**A save whose definitions are missing keeps them.** Loaded without a custom item's definition, its
copies still get numbers, orphans'. A copy with state of its own (an `instance`) gets a handle.
Copies without one share one orphan number per missing definition, from the definitions' range, so
they still sort together and stack in a quick slot as the item did, since retail stacks by
comparing numbers. Through either, retail's code and the screens see the fallback (a Dagger, with
the weapon's own level and attachments), while the registry keeps the copy's real item, instance
and state. The orphan travels and is saved like any other copy, so
saving over the game loses nothing, and the item is itself again once its definition returns. Only
a copy that is sold, used up or destroyed is gone. On loading such a save the game says which
definitions are missing and what stands in for them, and offers to go back; since nothing is lost by
saving, it needs no further prompt.

The load screen marks such a save before it is opened, with retail's exclamation mark: the "!" the
towns show above Toan (`CharaCursor2`, drawn in `editloop.cpp`), drawn on the save's board. The port
already draws those boards itself (`DrawMenuSave`, `port/src/memcard.cpp`), so the mark needs no new
screen; the boards move to the UI framework with the Main Menu and the save screens, and the mark with
them. Whether a save holds orphans is read from its extension file alone, the item IDs it names
checked against the registry, so the screen loads no save to show the mark.

A version of the port without this framework knows nothing of the extension file: it would save
`save.dat` alone, and the next load here would find the records stale.

### Keeping saves sound

Each of these is a way custom items could leave a save wrong, and what prevents it.

- **A runtime number never reaches a file.** Handles and custom definitions' numbers mean something
  for one run only. Before writing, the host strips them from every field of the save that holds an
  item number, found by an audit of the save's types (`savedata.hpp`, `userstatus.hpp`,
  `stockitem.hpp`, `itemdata.hpp`): the copies (pack, quick slots, carried attachments, weapons,
  holes, and storage's items, weapons, holes and attachments) and the fields
  that name a kind of item, such as an attachment's `sphere_weapon_no` and the fishing records'
  `fish_id`. Fields still named `unk_` are audited as they are named. Then the writer checks the
  stripped image: a field holding a number retail doesn't have is written as its fallback instead,
  and the log says where. A test fills every audited field with a handle and checks none survives.
- **References aren't copies.** Retail copies an item number into fields that name a kind of item:
  synthesis writes a weapon's number into the sphere it makes (`sphere_weapon_no`). A handle there
  would later name whichever copy reuses it. The functions that write such fields are replaced to
  write the definition's number, and the file names the definition by ID.
- **One copy, one handle.** Retail can copy a whole struct, glitches included (the Broken Dagger,
  #158), so the same handle can turn up in two places. The scan before writing finds it, the second
  place gets a new instance with a copy of the first's state, and the log says so.
- **Fallbacks that fit where the copy sat.** A fallback must be a vanilla item of the same kind, and
  for a weapon one the same character can hold, since weapons belong to characters
  (`chara_weapons[6][11]`). `Build` rejects any other.
- **Data can't reshape a save.** The fields that decide the shape of what a save holds, an item's
  kind and table index, a weapon's number of holes, a table's length, come from the extraction only;
  a document can tune values but not change these (section 6). Otherwise a pack could leave a saved
  attachment in a hole its weapon no longer has. The quick slots' rules are the one exception, and
  retail tolerates what they leave behind (section 10).
- **Writes leave a matching pair, whatever happens.** The container holds records for two hashes
  while a save is written: the `save.dat` on disk and the one being written. The host writes the
  container with both, then `save.dat`, then the container again with only the new one, each under a
  temporary name, flushed to disk and renamed into place. A crash at any point leaves a container
  holding the records of whichever `save.dat` is there, so no session's items are lost to it.
  Temporary files left by a crash are removed at start.
- **Retail's checksum covers what is written.** The host strips custom items before retail's save
  code computes `save.dat`'s checksum bytes, and puts them back in the live game after; a test
  checks every handle comes back.
- **A damaged chunk spoils only itself.** Each chunk has its own CRC. One that fails is set aside
  (copied to `chronicle.dat.damaged` for recovery), its copies take their fallbacks, and every other
  chunk loads: a bad items chunk costs no quest.
- **One writer.** A lock in the save folder stops two running copies of the game from interleaving
  their writes.
- **Definitions that change.** A copy's state is saved with its definition's state version, and a
  definition reading an older version migrates it (what happens to keys it no longer has is under
  "Keeping saves small").

### Keeping saves small

A save's own arrays bound how many copies it can hold, so what grows a save is data left behind:
values nothing reads any more. Some of that is kept on purpose, so content that comes back finds its
progress again, as the quest framework already keeps the progress of a quest that is no longer
registered (`quest_framework.cpp`). The rules keep that deliberate, never accidental, and let the
player let it go.

- **Records follow copies.** The scan before every write drops the record of any copy no save
  structure holds any more. An orphan's record lasts exactly as long as its copy.
- **Renamed fields don't pile up.** A copy's state carries its definition's state version. A
  definition that is present and knows that version (or a newer one) migrates the state and keeps
  only the keys it declares; the rest are dropped, and the log names them. Unknown keys are kept
  only while they may still mean something: when the copy's definition is missing, or the state was
  written by a newer version of it than the one loaded.
- **Orphaned data is kept, visibly.** Three kinds of data outlive what reads them: orphaned copies
  (section above), the chunk of a framework this port doesn't have (written by a newer port, or one
  since removed), and a quest's progress when the quest isn't registered. Each is kept and written
  back unchanged, so the content can return, and each is listed in the load notice and marked on the
  save's board with the orphaned items.
- **Orphaned data can be let go,** all of it at once, from the load notice or with `dcdata`: orphaned
  copies become their fallbacks for good, and the unread chunks and quest progress are removed.
- **No size limit, a size warning.** Refusing to save data would lose it, which is worse than a large
  file, so nothing is capped or cut. A copy's state or a chunk far larger than its framework
  normally writes is written in full and logged, which catches a framework that grows a list it
  should keep as a count. Frameworks keep counts and bests, not histories: fish caught per species
  and the largest, not a log of every catch.
- **Handles are reclaimed during play,** not only when saving: when three quarters of them are in use,
  the host frees every handle no structure holds, so a long session of buying and selling can't run
  out.
- **Housekeeping.** Temporary files a crash leaves are removed at start; a slot keeps at most one set
  -aside damaged chunk file, replaced by the next, and deleting the save deletes it.

---

## 10. Stacking and the quick slots

**First version: quick slots.** Any item can be allowed into a quick-use slot, made to stack there,
and given a maximum, through the `QuickSlot` trait. For vanilla items this is the `stack_kind`
field of the `items` document, which reaches `ITEM_DATA::stack_kind` through the loader, plus a
maximum, which retail has no field for and the host enforces, so most of retail's code needs no
change. The maximum is at most 99, the most the quick-slot counters can draw in their two digits.

A pack's quick-slot rules are the one thing a document changes that a save keeps: a stack built
under a raised maximum, or an item a pack let into a quick slot, stays in `save.dat` when the pack
is gone. Retail copes with that: it only counts a stack down as it is used, and won't add to one
past its own limit (`battlemenu.cpp`), so the stack is simply used up.

**Later: several copies in one bag slot.** The 103-slot pack holds one item per slot throughout
retail: pickup, sorting (`SeitonItemBoard`), the overflow check, every item menu, shops and storage.
Stacking in the bag is a phase of its own, with its own design pass, on the rebuilt inventory
screens (section 11).

---

## 11. The inventory screens

The screens that show and move items are where most of the work for custom items is, so they move
to the UI framework (`UI::`, `port/include/ui_framework/`) as part of this work rather than being
patched.

**Today.** The battle menu's ring, weapon and move screens already draw through the UI framework
(`port/src/battlemenu/`). The item screens are still retail's:

| Screen | Retail | Range comparisons in its unit |
|---|---|---|
| Item menu: the bag, quick slots, using and discarding | `ItemMenuMainKey`, `ItemMenuModeKey`, `ItemMenuModeDraw`, `ItemTrushKey` (`battlemenu.cpp`) | 61 |
| Shops | `shop.cpp` (with the port's `custom_shop.cpp` for quest shops) | 96 |
| Storage | `stockitem.cpp` | 11 |
| The item picker events and fishing open | `InitEventItemSelect`, `EventItemSelectLoop` (`menu_save.cpp`) | 9 |
| Item boards and icons the menus share | `menu_draw.cpp`, `menu_dungeon.cpp` | 35, 15 |

**Why rebuild rather than patch.** A custom item has to be listed, drawn, described, sorted, used,
sold and stored by these screens. Patched in place, each of their range comparisons and per-item
`case` labels would be replaced to ask the registry, in functions of thousands of lines. Rebuilt on
the UI framework, a screen asks the registry from the start: what an item is, its icon, its name,
whether it may go in a quick slot, how many fit, so custom items cost the screens nothing extra, and
the bag stacking of section 10 has screens that can show a count.

**Shared widgets first.** The screens have the same parts, so the UI framework gains them once: an
item grid with a cursor and scrolling, an item cell (icon from the registry, custom icons included,
and a count), the item's name and description panel, and the quantity prompt. The fishing
framework's bait picker (phase 2) is their first user and the smallest screen, which makes it the
place to settle them.

**Parity.** Each rebuilt screen keeps retail's layout, controls and sounds, as the battle menu's
rebuilt screens did, and is compared with retail's before the old one is retired: the same items in
the same order, the same prices, the same results for every action.

---

## 12. The other frameworks

**Fishing** is the first user. Its bait picker lists every item with the `Bait` trait and attaches
the chosen bait in C++. That also mends retail PAL's bug in which Carrot, Potato Cake, Minon, Battan
and Petite Fish are consumed but never attached: the towns' event scripts only accept eight of the
thirteen baits (program 134 in `gedit/e01`, `e02`, `e04` and `s09`). The script goes on after the
picker: for the eight baits it recognises it attaches the bait itself (`_LOAD_ITEM`,
`_SET_FISHING_ESA`), and for any other it goes straight to `_GOTO_FISHING`. The framework's attach,
which covers all thirteen and custom baits, is the one kept, so the script's own is skipped and no
bait is attached twice. A town catch never becomes an
inventory item in retail (it becomes fish points and a ranking record), so the `Fish` trait is for
the fish retail does keep as items (`ITEM_SORT_FISH`) and for custom fish the player may keep.

A catch is published as an event: species, size, fish points, place and bait. Retail decides these
at `ED_FISHING_MES_EVALUATE` in `ebattle.cpp`, where it calls `SetFishingRank` and
`AddFishingPoint`.

**Quests** gain conditions that read those events ("caught three of a species", "one at least 40
long", "one with Golden Lure"). The fishing framework registers them with the quest framework, so
the quest framework never depends on fishing, as dialog providers register with `Dialog::`. The
weapon framework can add its own ("a weapon at level 10") the same way. `Quest::IGame`'s
`ItemCount`, `GiveItem`, `TakeItem` and `HasItemAnywhere` take item IDs as well as numbers, so a
quest can ask for an item defined in JSON.

**Shops** get a framework of their own later. An item's value, its buy and sell price, belongs to
the item, since any item can be sold at any shop, and comes from the `prices` document. Which shops
stock what, when, at what markup, and quest shops (today `custom_shop.cpp`) are the shop framework's.

**Weapons** are items with the `Weapon` trait. This framework owns a weapon's identity, handle and
saving; the weapon framework owns what a weapon does: build-up paths, synthesis, abilities, stat
growth. Custom attachments are the `Attachment` trait, with their per-copy state in the
`ATTACH_LIST` copies retail already keeps.

---

## 13. Icons and models

**Icons** are part of custom items, since one can't be shown without an icon. A custom item's row
names a PNG in the same tree, and the host adds them to the item-icon texture at load. Custom items get icon indices past
retail's, and the four places that read `icon_index` ask the registry.

**Models** are a reference in each definition: any retail item's model (`"model": "vanilla:minon"`),
or an MDS file in the same tree. Model paths are built in one function, `BtGetItemNamePath`, so
that is the one place that changes. Importing glTF or other formats is left for a later project with
Kazarov: it means turning a modern model into the game's own frame data, which is renderer work. The
definition format won't need to change for it.

---

## 14. Phases

Each is its own pull request, working and tested on its own.

1. **Documents and registry.** ChronicleTwo's datapack index brought across, with folder packs
   (section 7), the runtime extraction and
   the loaders for the item tables, `--export-gameplay`, the `Items::` registry and traits, and
   quick-slot stacking. No new item numbers yet, so the 276 range comparisons are untouched.
   Already useful: prices, quick-slot rules, heal amounts and baits can be changed from a pack.
2. **Fishing**, on phase 1: the `Bait` and `Fish` traits, the bait fix, catch events, and the quest
   conditions. Its bait picker is the first screen on the shared item widgets (section 11).
3. **Custom items.** Custom item documents (section 7), custom numbers and handles, the save's
   extension file (section 9) and icons;
   the inventory screens
   rebuilt on the UI framework (section 11), one pull request per screen: the item menu first, then
   storage, the shops and the item picker; and the remaining range comparisons outside the screens
   replaced to ask `Items::KindOf`.
4. **Weapons**, on phase 3: the weapon framework.
5. **Bag stacking** (section 10).

---

## 15. Tests

- The extraction proves itself: extracting the tables and loading the documents back leaves every
  table byte for byte as compiled (`ComItemInfo`, `ITEM_LIST`, `AttachList`, `WeaponList`,
  `esa_info`, `PriceList`).
- With no custom items, a save is byte for byte retail's.
- A field a pack changes reaches retail's table, and the compiled value comes back without the pack.
- Datapacks: a later pack wins over an earlier one and over the extraction; a path a pack lacks
  falls through; a candidate with a bad field, the wrong `kind` or a newer `schema_version` is
  reported and the next one used; an unknown field is kept. ChronicleTwo's `datapacks_test.cpp`
  comes across with its code.
- `--export-gameplay` writes documents that load back to the same tables.
- Custom item documents: two packs each adding an item both load; a later pack's file for the same
  item replaces the earlier one's; a pack overrides an item the port defines in C++.
- Saving: custom copies move to the extension file and back, from every place in section 9's `at`
  list, storage included; a missing definition gives an orphan that survives saving and is itself
  again when the definition returns; orphaned copies without state share one number and stack in a
  quick slot; an unused handle is dropped.
- Soundness (section 9): a handle placed in every audited field of a synthetic save never reaches
  `save.dat`; a duplicated handle is split into two instances; a crash simulated after each step of a
  write leaves a container matching the `save.dat` on disk; a damaged chunk costs only its own data;
  handles are reclaimed under pressure.
- Orphaned data: a migrated state keeps only declared keys; unknown keys survive while the definition
  is missing; a chunk no framework reads and an unregistered quest's progress are written back
  unchanged; letting orphaned data go removes all three kinds and nothing else.

As elsewhere in `port/src/tests/`, fixtures are made in code: synthetic documents, packs, saves and
tables, no disc data.

---

## 16. Open questions

None at the moment.
