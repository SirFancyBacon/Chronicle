# Quality of life

Small changes to how the game plays, each off unless a datapack turns it on, so without one the game
plays as retail. The game ships them as the Quality of Life pack (`port/datapacks/qol/`,
`port/datapacks/README.md`).

## The changes

| Change | What it does | Where |
|---|---|---|
| Party pool restoration | Healing water and heal zones refill every party member's HP and water, not only the one walking in. A member at 0 HP is left as they are. | `HealingWater` (`port/src/game/shot_freefuncs.cpp`) |
| Inventory auto repair | When a weapon would break, an Auto Repair Powder in the bag saves it as one in a quick slot does. One in a quick slot is still used first. | `BattleSubWeaponDmg` (`port/src/game/hitvalue.cpp`) |
| Quick slot Repair Powder | Repair Powder goes in a quick slot, copies stacking, and using it repairs the equipped weapon as it does on the item board (a broken starting weapon comes back whole). It is used only on a weapon that is worn. | the item patch below; `checkItemUsed`, `RepairEquippedWeapon` (`port/src/game/itembombeffect.cpp`), the quick-use kind `ACTIVE_ITEM_REPAIR` |

Using Repair Powder from a quick slot plays the repair sound; retail has no message for it.

The pack also turns on two of the dungeon's own changes, which the game has off without it
(docs/DUNGEON.md), in `gameplay/dungeon/rules.json`:

| Change | What it does |
|---|---|
| Key item filter | A dungeon event's item window lists only the items it takes: a floor's exit its item, a back floor's entrance its item, a locked door its key. |
| Auto-open locked doors | Cross at a locked room door opens it when the bag holds its key. |

## Documents

The first two changes are switches in `gameplay/qol/features.json`, read as the item documents are
(docs/ITEM_FRAMEWORK.md, "Datapacks"): the highest-priority usable document, then the data's own, or
every switch off. A switch left out is off.

```json
{
  "schema_version": 2,
  "kind": "qol_features",
  "party_pool_restoration": true,
  "inventory_auto_repair": true
}
```

The third is data: retail decides from an item's `stack_kind` whether it may go in a quick slot, and
the pack patches Repair Powder's (`gameplay/items/items.patch.json`, `{"item_no": 177, "stack_kind":
0}`). Retail's 2 keeps it out, so the game's repair from a quick slot is only reached through such a
patch.

`darkcloud --export-gameplay <directory>` writes `gameplay/qol/features.json` with every switch off.
