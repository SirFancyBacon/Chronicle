# Combat Framework

Dark Cloud's hits say how much damage they do, but not what made them. The combat framework
(`port/src/combat/`, `dc::combat`) keeps a description beside every hit: who dealt it and with which
move. It can also take a monster from its script to play a reaction, such as a knockdown. Later
parts build poise, the guard's stamina and damage rules on these.

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

## Enemy control

`port/src/combat/control.hpp` lets the framework take a monster from its script, play a sequence of
its own motions by name, and hand it back. A knockdown and a stagger are built on it.

**How a monster runs.** Each tick, `CMonstorUnit::Step`:
1. checks hits (`CheckDmg`). A hit starts the script's hit handler (program 110) and a kill its
   death handler (120), dropping whatever the script was doing;
2. runs a step of the script, unless the stop status holds it. A finished script starts its loop
   (program 100) again.

A monster's attacks are damage collisions its script makes once, each hurting only while the
motion's frame is in its window. Playing another motion stops an attack without cleaning anything
up.

**A hold** (`dc::combat::Hold`) takes a slot's monster from its script:
- **The script is stopped.**
  - It doesn't step.
  - A hit doesn't start the hit handler. The hit still deals its damage, with its number, sound and
    red flash.
  - Each hit gives 8 steps of immunity, as a common monster's hit handler would. One swing makes a
    hit every tick of its window, so this keeps it from landing more than once.
- **The monster stands still:** no walking, turning or knockback.
- **It plays the reaction's steps,** each a motion by its name in the model's key table
  (`dc::combat::MotionIndex`). A step plays once or loops for a number of ticks:
  - the first step snaps into its motion and holds its last frame, as a hit reaction does (mode 6);
  - later steps blend in (mode 2);
  - loops loop (mode 0).
- **A reaction can move the lock-on** to a frame of its own while it lasts.
- **At the end, the script starts its loop again,** as after its hit handler.
- **A kill ends the hold** and runs the death handler as usual. A stop status pauses the hold with
  the script.
- **Holds are dropped** when the floor's monsters are cleared (`CleanViewMonstor`) or a slot takes a
  new monster (`SetupViewMonstor`).

**Reactions:**

| Reaction | For | Steps |
|---|---|---|
| Down (`DownReaction`) | a model whose `damage_2` and `get_up` have frames of their own | `damage_2`, then `get_up`. For a model whose frames are known, 2 s in all: the fall and the rise at their own speed, the roll on the ground sped up (at most 3×), the lying still left out, and the monster back to its script once on its feet. Another model plays both whole |
| Stagger (`StaggerReaction`) | Master Utan (`c14a`) | `damage_left_foot_enter`, `panting_loop` for 120 ticks, `damage_left_foot_return`; the lock-on on his `face`, as in retail when he is hit on a foot |
| Stagger | another model with a hit reaction | its first of `damage_1`, `damage`, `damage_6_1`, `damage_face`, `damage_eye`, then 50 ticks of its idle |

Many models keep `damage_2` and `get_up` as placeholders on their idle frames (the Dasher, the Golem,
the Lich, the Gemron...). A reaction uses a motion only when it has frames of its own: some, and not
the idle's (the key table's first, from `dc::combat::MotionKeys`), so those models stagger instead of
falling. The models that can fall are the Master Jacket, the Skeleton Soldier, the weekday enemies
(Sunday to Saturday), the card soldiers (Heart, Club, Diamond, Spade, Joker), the Mummy, the Curse
Dancer, the Hell Pockle, the King Prickly, the Horn Head, the Bishop Q, the Silver Gear and Gacious.

A down's frames (where it reaches the ground, lies still and is back on its feet) are known for three
families of models, read off the height of their heads: `e01a`, `e03a`, `e50a`, `e129a`, `e131a`; `e44a`
to `e48a`, `e132a`, `e138a`, `e144a`, `e150a`, `e161a`; and `e52a` (`kDownMarks` in `control.cpp`).

**A downed monster is hit as columns.** Its spheres follow its bones, so lying down puts them on the
floor, under Toan's level swings. While it is down, each sphere reaches any hit as near across the
ground as the two radii and at any height its spheres spanned as it stood when it went down
(`ReachesDowned`), as retail tests the player's body.

**Nothing starts a reaction in play yet;** poise will. `DC_COMBAT_TEST=down` shows them: a
character's finisher or charged attack that leaves a monster standing downs it if it can fall, and
staggers it otherwise.

## Trace

`DC_COMBAT_TRACE=1` prints each hit as it lands, on standard error:

```
combat-trace: Toan finisher (5) -> e03a[3]: 18 dmg, 0 poise, 0 stamina
combat-trace: e03a[3] attack_1/dcol0 -> Toan: 4 dmg, 0 poise, 0 stamina
combat-trace: e03a[3] attack_1/dcol0 -> Toan (blocked): 0 dmg, 0 poise, 0 stamina
```

A monster is named by its model and slot. A hit on the player says when the guard stopped it.
Enemy control adds a line as a monster is held (`e03a[3] down`) and as it goes back to its script.

## Tests

`port/src/tests/combat_hits_test.cpp`:
- a description follows its slot, and `CCollisionData::Set` hands out a slot with none;
- slots out of range are ignored;
- values come from the source, and are 0 without one;
- attack keys from a synthetic key table;
- the trace's names.

`port/src/tests/combat_control_test.cpp`:
- the down and stagger reactions from the Skeleton Soldier's and Master Utan's key tables;
- a hold's steps: one played once ends at its motion's end, but never on its first tick; a loop
  ends after its ticks; then the hold releases;
- releasing one slot or all.

The tags at each attack window are checked in play, with the trace.
