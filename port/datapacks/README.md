# Datapacks

Packs kept with the port for trying out. Nothing builds or installs them: the game reads packs from
the `datapacks/` folder beside its data folder, and a pack is on when it is there and named in that
folder's `datapacks.json` (docs/ITEM_FRAMEWORK.md, "Datapacks"). Packs are read at start-up.

## Enhanced fishing

`enhanced_fishing/`: Dark Cloud 2's brisk bite and tension fight, and Ruty's three rods with her
quest (docs/FISHING_FRAMEWORK.md, "Enhanced fishing").

With the Flatpak, whose data folder is `~/.var/app/org.themoonpeople.Chronicle/data/chronicle/data`:

```sh
packs=~/.var/app/org.themoonpeople.Chronicle/data/chronicle/datapacks
mkdir -p "$packs"
cp -r port/datapacks/enhanced_fishing "$packs/"
echo '{ "schema_version": 1, "load_order": ["enhanced_fishing"] }' > "$packs/datapacks.json"
```

Elsewhere, `datapacks/` sits beside whichever data folder the game uses (`--data`, `DC_DATA`, or the
folder beside the executable). Taking the pack out of `load_order`, or the folder away, goes back to
the base game's fishing; a save holding Ruty's rods keeps them as unknown items until the pack is back.
