#!/usr/bin/env bash
# Brings a Linux release folder up to date with a build: release.sh <build dir> <folder>.
# The data and save folders a player's runs leave in it are kept, and no game data comes from here, so a fresh folder holds nothing from a disc.
set -euo pipefail

build=$1
folder=$2

# Named one by one, so that a build missing any of them fails here and not later.
for name in darkcloud dcdata lang glyphs assets/data.zip assets/datapacks; do
    if [ ! -e "$build/$name" ]; then
        echo "release: the build has no $name" >&2
        exit 1
    fi
done

mkdir -p "$folder"
for name in lang glyphs assets; do
    rm -rf "${folder:?}/$name"
    cp -r "$build/$name" "$folder/"
done
cp -f "$build/darkcloud" "$build/dcdata" "$folder/"
# SDL3 is the one library most machines won't have. The rest come with any recent system.
sdl=$(ldd "$build/darkcloud" 2>/dev/null | awk '/libSDL3/ { print $3; exit }' || true)
if [ -z "$sdl" ] || [ ! -e "$sdl" ]; then
    echo "release: can't find the SDL3 that $build/darkcloud uses" >&2
    exit 1
fi
cp -fL "$sdl" "$folder/"

cat > "$folder/play.sh" <<'SCRIPT'
#!/usr/bin/env bash
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
LD_LIBRARY_PATH=".${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" exec ./darkcloud --data data --save save "$@"
SCRIPT
chmod +x "$folder/play.sh"
echo "kazarov: release folder $folder"
