# Fishing Framework

Dark Cloud's fishing tables are compiled into the game. The port turns them into JSON documents that
datapacks can replace, as the item tables are (docs/ITEM_FRAMEWORK.md), and keeps the species and
places in registries (`dc::fishing`, `port/src/fishing/`) that retail's fishing code asks.

With no datapacks fishing plays as retail: the same fish at the same places with the same odds,
points, names and prizes, except where the port makes the bite readable and fixes a size roll:

- A fish that takes the hook holds it for half a second to a second (`kHookHoldFirst`,
  `kHookHoldLast` in `port/include/game/fish.hpp`); retail's third of a second to five sixths left
  too little time to press Cross.
- The moment it takes the hook is marked by a splash, rings at the float and a strong rumble
  (`FishingBiteCue`, `ebattle.cpp`), where retail's tug and rumble were much like a nibble's.
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
                       { "fish": "vanilla:fish_6", "weight": 1 } ] } }
```

- `fish_count` is 1 to 6; `draw_under_water` is false only at the sea, whose fish aren't drawn.
- `spawn` names the stage that chooses the fish, with its settings beside the name. The `retail`
  stage draws each fish from `fish` by weight, or, one time in the time of day's `interval`, from
  `rare`. Retail rolled each spot its own way; the odds are the same (`FishingDocuments.EachPlaceSpawnsWithRetailsOdds`),
  the draws for a given seed are not.
- Where the water is stays in the town scripts, which pass its bounds and level; a document changes
  the places they open, not adds new water. Debug mode's fill of every spot stays retail's.

**`fish_prizes`**, the exchange's 35 rows in its order, `{ "item": "vanilla:antidote_drink", "price": 5 }`:
any item by ID, and its price in fish points. The exchange screen draws a fixed list, so a document
changes prizes and prices, not how many there are.
