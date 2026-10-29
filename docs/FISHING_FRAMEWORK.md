# Fishing Framework

Dark Cloud's fishing tables are compiled into the game. The port turns them into JSON documents that
datapacks can replace, as the item tables are (docs/ITEM_FRAMEWORK.md), and keeps the species and
places in registries (`dc::fishing`, `port/src/fishing/`) that retail's fishing code asks. A fishing
session is the framework's too: retail's steps, moved out of `EdMoveChara`, asking the place's stages
what happens at each.

With no datapacks fishing plays as retail: the same fish at the same places with the same odds,
points, names and prizes, except where the port makes the bite readable and fixes a size roll:

- A fish that takes the hook holds it for half a second to a second (the `retail` bite's
  `hook_ticks`); retail's third of a second to five sixths left too little time to press Cross.
- The moment it takes the hook is marked by a splash, rings at the float and a strong rumble
  (`BiteCue`, `fishing/angler.cpp`), where retail's tug and rumble were much like a nibble's.
- Sizes near a kind's largest are ramped into it rather than piling onto it (`SmoothFishSize`).

## Documents

Each table is one document under `gameplay/fishing/`, read like the item documents: from the
highest-priority pack down, then the data's own, then the table as compiled. They are tracked in
`port/gameplay/fishing/`, sealed into the gameplay seed with the item documents, and written by
`darkcloud --export-gameplay <directory>`.

| Path | `kind` | Rows (key) | Table |
|---|---|---|---|
| `fish.json` | `fish` | `fish` (`fish_kind`) | `fish_info`, with each kind's model (`GetFishFileName`) and name message (`GetFishMsgNo`) |
| `places.json` | `fishing_places` | `places` (`spot`) | `FishingLoadFish`'s spots, fish counts and spawn odds |
| `prizes.json` | `fish_prizes` | `prizes` (in order) | `exitemlst`, the fishing exchange |

A document must hold every row of its table and every field of each row; one that cannot be used
prints `config: <path>: <reason>` and the next is tried.

**`fish`**, one row per retail kind, 0 to 17:

```json
{ "fish_kind": 5, "id": "vanilla:fish_5", "model": "chara/f06a.chr", "name_msg": 45,
  "model_size": 21.0, "min_size": 10.0, "max_size": 16.0, "min_fp": 200, "max_fp": 400,
  "food_affinity": [ { "bait": "vanilla:poisonous_apple", "affinity": 1.0 } ],
  "mardan_garayan": true }
```

- `min_size` is the kind's usual size, the centre of `CFish::SetScale`'s roll; points are linear in
  the size from `min_fp` to `max_fp`.
- `food_affinity` names baits by item ID (an item with the `Bait` trait); a bait left out is not
  liked. A fish notices a bait at its liking × 0.3 and takes it at × 0.5, so 2 is the most that
  means anything.
- `mardan_garayan` counts a catch toward the Mardan Garayan (retail: kinds 5 and 17).
- The kinds and their IDs are fixed: the save's records name a fish by kind.

**`fishing_places`**, one row per spot the town scripts open (0 Norune, 1 and 2 Matataki, 3 Queens,
4 Muska Lacka):

```json
{ "spot": 2, "id": "vanilla:matataki_2", "fish_count": 5, "draw_under_water": true,
  "spawn": { "stage": "retail",
             "rare": { "interval": { "day": 50, "evening": 20, "night": 35, "morning": 25 },
                       "fish": [ { "fish": "vanilla:fish_17", "weight": 1 }, { "fish": "vanilla:fish_5", "weight": 4 } ] },
             "fish": [ { "fish": "vanilla:fish_2", "weight": 1 }, { "fish": "vanilla:fish_4", "weight": 1 },
                       { "fish": "vanilla:fish_6", "weight": 1 } ] },
  "bite": { "stage": "retail", "hook_ticks": [30, 60], "lose_bait_early": 0.2, "lose_bait_escaped": 0.3 },
  "fight": { "stage": "retail", "ticks": 120 },
  "outcome": { "stage": "retail" } }
```

- `fish_count` is 1 to 6; `draw_under_water` is false only at the sea, whose fish aren't drawn.
- `spawn` names the stage that chooses the fish, with its settings beside the name. The `retail`
  stage draws each fish from `fish` by weight, or, one time in the time of day's `interval`, from
  `rare`. Retail rolled each spot its own way; the odds are the same (`FishingDocuments.EachPlaceSpawnsWithRetailsOdds`),
  the draws for a given seed are not.
- `bite`, `fight` and `outcome` name the place's other stages (below).
- Where the water is stays in the town scripts, which pass its bounds and level; a document changes
  the places they open, not adds new water. Debug mode's fill of every spot stays retail's.

**`fish_prizes`**, the exchange's 35 rows in its order, `{ "item": "vanilla:antidote_drink", "price": 5 }`:
any item by ID, and its price in fish points. The exchange screen draws a fixed list, so a document
changes prizes and prices, not how many there are.

## Custom species

A pack adds a species with a document of its own, `gameplay/fishing/species/<namespace>.<name>.json`
(the data may hold some too, read under the packs), as custom items are added:

```json
{
  "schema_version": 2,
  "kind": "fish_species",
  "id": "chronicle:moon_trout",
  "model": "chara/f06a.chr",
  "model_size": 21.0, "min_size": 12.0, "max_size": 24.0,
  "min_fp": 50, "max_fp": 90,
  "food_affinity": [ { "bait": "vanilla:carrot", "affinity": 1.0 } ],
  "mardan_garayan": false,
  "text": { "en_gb": { "name": "Moon Trout" }, "fr_fr": { "name": "Truite de lune" } }
}
```

The fields are a `fish.json` row's (with `item` optional, as there), the name by language file (falling back to English, then
the first given) in place of `name_msg`, and `mardan_garayan` false if left out. The file's name must
be the ID with `.` for `:`, and `vanilla:` is retail's. Custom species get kinds from 256
(`fishing::kCustomKindFirst`) in ID order, and a place's spawn names them like any species. The catch
message gives the name by laying it over the evaluation window (`ClsMesSetMessageOverlay`), since the
towns' message files have none.

**Records.** `save.dat`'s `fish_data` names retail kinds, so a custom species' catches go instead into
`chronicle.dat`'s `FISH` chunk (docs/PC.md, "Saves"): for each species, by ID, how many have been
caught, the largest and the baits it has been caught with (`fishing::RecordOf`). Records of a species
whose document is gone are kept and written back until it returns. Its points still go to the save's
fish points, which are only a number.

## Baits

Any item with the `Bait` trait is a bait: retail's thirteen, and custom items whose document has a
`bait` (docs/ITEM_FRAMEWORK.md, "Custom items"). The bait picker offers every one the player carries,
and the town's script attaches the one chosen with its own code, the port putting a bait its list
lacks on it (`ListBaitForScript`, which fixed PAL's five baits that were eaten and never attached).
`esa_type` and a fish's `food_kind` index the framework's list of baits (`fishing::BaitAt`): retail's
in `esa_info`'s order, then the custom ones.

A fish that takes the hook eats the bait, as retail. A lure (`"lure": true`) comes back to the bag
instead and is never lost to a nibble or a fish that lets go; it has to come off the hook as the fish
is landed, because the landed fish's model is put where the town's script built the bait's (the
event buffer starts at the villagers' buffer's free end), so it is put on again from the picker. A species that doesn't name a bait in its `food_affinity` likes it at the bait's
`default_affinity`. Leaving fishing or changing the bait gives back the one on the hook, as retail.

## Stages

What happens at a place is decided by four stages, each chosen by name in its row with its settings
beside the name (`port/src/fishing/framework.hpp`; a new one is a class and a line in
`RegisterBuiltinStages`, `stages.cpp`). The port ships `retail` of each:

| Stage | Decides | `retail` |
|---|---|---|
| spawn | the fish in the water when fishing starts | each fish drawn by weight, the rare table one time in `interval` |
| bite | what each fish does about the bait, asked by `CFish::Step` when its last action has run out, and when the bait is lost (never a lure) | notices at its liking x 0.3, takes the bait and the hook at x 0.5, loses interest at 0.1, every 21 to 50 ticks; holds the hook `hook_ticks`; loses the bait at `lose_bait_early` for Cross on a nibble and `lose_bait_escaped` for a fish that lets go |
| fight | from the hook set to the fish landed | none: the controller rumbles for `ticks` while the fish's model loads |
| outcome | what a landed fish gives | the Mardan Garayan count, the points, the records and the evaluation message |

The port also ships a `keep` outcome: the fish goes into the bag as its species' `item` (below), and
gives its points and goes in the records (with the Mardan Garayan count) only where the place's
settings say `"points": true` and `"records": true`. The evaluation message still shows, with no points
unless they were given. A full bag overflows as an event's gift does, the player choosing what to
throw away. A species without an item isn't kept.

## Kept fish

A species row or document may name an `item` (an item ID, or null): what one of its fish becomes at a
`keep` place. Where the item's document declares `state` with a `size`, each kept fish is a copy of its
own with its size there (docs/ITEM_FRAMEWORK.md, "Copies"); an item with `sort_key` 7 has the `Fish`
trait, so a quest's `HasItemWith<item::Fish>` counts it. Retail's places don't keep: turning that on is
a pack's `places.json`.

## The session

`fishing::Session` (`session.cpp`) is retail's `EdMoveChara` fishing block, one tick at a time:
standing (Cross casts, Circle and Square open the town's menus, events 0x85 and 0x86), casting,
waiting with its idle motions, the nibble, the bite, reeling in, the fight, the result and putting the
fish away. It has no game headers: everything it does in the game goes through `fishing::Angler`,
which `angler.cpp` implements with the angler, the line, the fish, the camera, the sounds and the
messages, and tests implement with a record of what was asked (`FishingSession.*`). `EdMoveChara`
calls `fishing::StepSession`, and `chara_fishing` still holds the step for the game's code that reads
it.

## Events and quests

Every landed fish is published once, after its outcome, to listeners added with `fishing::OnCatch`:
its species, size, points, place, bait, time of day and whether it was kept. A fish that got away is
published to `fishing::OnLost` listeners with why (Cross on a nibble, or letting go of the hook) and
whether the bait went with it. A listener can't change either; what a catch gives is the outcome
stage's.

Quests count catches in their own numbers, which `quest::VarAtLeast` reads and the save keeps with
their progress (`port/src/fishing/quests.hpp`, through `quest::ChangeVar`), from when the helper is
called, usually as the quest is registered:

| Helper | Keeps in the quest's var |
|---|---|
| `CountCatches(quest, var, filter)` | how many catches match |
| `CountFishPoints(quest, var)` | the points from fishing |
| `KeepLargest(quest, var, filter)` | the size of the largest catch that matches |
| `CountSpecies(quest, var, species)` | how many of the listed species have been caught, each once |

A `CatchFilter` matches by species, place, bait and a least size, each left empty to match any. A kept
fish is an item too, so "bring me a fish" is the quest framework's `HasItemWith<item::Fish>`.
