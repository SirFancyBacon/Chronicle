# Combat Framework

Dark Cloud's hits say how much damage they do, but not what made them. The combat framework
(`port/src/combat/`, `dc::combat`) keeps a description beside every hit: who dealt it and with which
move. Later parts build poise, the guard's stamina and damage rules on it.

With no datapacks the game behaves exactly as retail: nothing here changes a number.

## Hits

The game keeps its hits in `CCollisionData` (`NowColData`): 96 records, each made by `Set` and
freed when its `life` runs out or it lands. A swing makes a fresh record on every tick of its
window. A record carries damage, owner, kind, status flags, the weapon's family table and knockback,
but not the move:
- Toan's five combo hits all have `attack_no` 0, the finisher included.
- A monster's `attack_no` is the order its script made its damage collisions in.

`dc::combat::Hit` describes the record in the same slot:

| Field | What it holds |
|---|---|
| `attacker` | `Character`, `Monster`, `Item`, `Bomb`, `Floor`, or `None` for a hit no site described |
| `who` | the character (0 Toan to 5 Osmond) or the monster's slot |
| `move` | `Combo`, `Finisher`, `Charged`, `Shot`, `Thrown`, `Blast`, `Trap` or `Attack` (a monster's) |
| `step` | the combo step or the charge level, from 1 |
| `model`, `motion`, `frame` | a monster's model, the motion it was playing, and the frame its damage collision rides on |
| `poise`, `stamina` | the poise and stamina damage it deals, from the value source (below) |

`CCollisionData::Set` resets its slot's description, and the site that made the hit describes it
straight after (`dc::combat::Describe`). Readers look a slot up with `dc::combat::Find`.

### Where hits are described

| Source | Where | Description |
|---|---|---|
| Toan | `ToanKey_Play` (`charakey.cpp`) | combo 1 to 4, finisher 5 (the ×1.8 hit), charged 1 (`charge_attack_loop`), charged 2 (`skill_2`) |
| Ungaga | `UngagaKey_Play` | combo 1 and 2, finisher 3 |
| Goro | `GoroKey_Play` | combo 1 (his swing), charged 1 (`charge_attack_release`) |
| Xiao | `CSHOT::step` (`shot_effect.cpp`) | shot |
| Osmond | `CSHOT_MACHINGUN::Step`, `shot_firebar.cpp` | shot |
| Shot effects | `CSHOT_EFFECT::Step` | a character's shot (Toan's beam, Ungaga's and Goro's effects, Ruby's magic), a monster's projectile, or an item's (the gems) |
| Thrown items | `mainitemmodel.cpp` | item, thrown |
| Bombs | `itembombeffect.cpp` | bomb, blast |
| Traps | `_SET_STATUS_ERR` (`btsysscript.cpp`) | floor, trap |
| Monsters | `CMonstorUnit::CheckDmg` (`DescribeMonsterHit`) | monster, attack |

Charged shots are described as shots: a shot effect doesn't keep its charge.

### A monster's attack key

`dc::combat::AttackKey` names a monster's attack `<motion>/<frame>`, for example `attack_1/dcol0`:
the motion it was playing when the hit was made, and the frame its damage collision rides on
(`_SET_DMG_COL`'s frame, without the `__` flags in its name). The key stays the same however the
script orders its collisions. A projectile is keyed `shot`, since by the time it lands the monster
may be playing another motion.

Retail's model scripts number motions only. The names come from the extracted
`dun/monstor/<model>.game.json`, whose `KEY` commands list them in the scripts' order (dummies
included). They are read once a model, and only when a key is asked for. Without the file, the
key gives the motion's number.

### Values

A hit's `poise` and `stamina` come from the value source (`dc::combat::SetValueSource`), called as
the hit is described. None is set yet, so both are 0.

## Trace

`DC_COMBAT_TRACE=1` prints each hit as it lands, on standard error:

```
combat-trace: Toan finisher (5) -> e03a[3]: 18 dmg, 0 poise, 0 stamina
combat-trace: e03a[3] attack_1/dcol0 -> Toan: 4 dmg, 0 poise, 0 stamina
combat-trace: e03a[3] attack_1/dcol0 -> Toan (blocked): 0 dmg, 0 poise, 0 stamina
```

A monster is named by its model and slot. A hit on the player says when the guard stopped it.

## Tests

`port/src/tests/combat_hits_test.cpp`:
- a description follows its slot, and `CCollisionData::Set` hands out a slot with none;
- slots out of range are ignored;
- values come from the source, and are 0 without one;
- attack keys from a synthetic key table;
- the trace's names.

The tags at each attack window are checked in play, with the trace.
