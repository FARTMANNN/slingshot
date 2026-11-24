#!/bin/bash
# Slingshot: Squadala! (co-op, github.com/daurosk/oot-remake-multiplayer) and its randomizer (OOTRR, github.com/ootrando/ootrr) on the Mac, built from
# their own source at the releases the PC kit runs, so a Mac player is one more Squadala! player: the same module, the same protocol, the same seeds.
# Run once, and again when the pins below move to a new Squadala! release:
#   bash dm/slingshot/tools/squadala_setup.sh [<game romfs>] [--pack]
# 1. Squadala! v0.11.12 (its tag's commit, checked) into ~/OcarinaNative/src, with Slingshot's macOS changes (dm/slingshot/patches/squadala-macos.patch: the
#    .dylib build, the Mac's clipboard, the pause menu's settings button named for Slingshot, and the pack's switch that leaves out the Characters tab,
#    whose code patch Slingshot cannot build in), built into ~/Library/Application Support/Slingshot/coop/hookshot_coop.dylib: the module the PC kit's
#    Longshot loads, which Slingshot loads with the game when it is there (dm/slingshot/src/coop/bridge.c). SQUADALA_SRC: another source tree, as is.
# 2. OOTRR v0.8.2, the randomizer Squadala! 0.11.12 runs (its tools/release/kit/hookshot/OOTRR.txt), from its tag (commit checked), built with cargo as
#    OOTRR's own release builds do (--release --locked), into coop/tools/ootrr, where the module finds it. Its Mods tab builds seeds with it exactly as on
#    Windows and Linux (ootrr generate + ootrr build --host hookshot), from your own game files (coop/romfs: a link to them).
# 3. Squadala!'s mods are Slingshot's: coop/mods is a link to Slingshot's mods folder, so the randomizer's seeds and the Mods tab's list
#    (coop-mods.list) are what Slingshot's Mods page and loader read (dm/slingshot/src/mods.h).
# 4. The puppet pack (how you see the other players) from your own game files with Squadala!'s own tool, with the title's Host, Join and Co-op Settings
#    rows and the pause menu's pages (--title-rows --pause-tab, as the kits), into mods/coop-puppet; again when this release's pack is newer than the
#    one there, or with --pack.
# 5. Squadala!'s pack tools (tools/puppet_pack) into coop/tools, and the Python that runs them (coop/python.txt): Slingshot runs their self_tunic.py and
#    merge_tables.py at each start, as the kits' starts do (your Link's tunic files; the size tables, the puppets' rows in other mods' actor tables, the
#    pack's menu files only where they can load).
# 6. Homebrew's libopus beside the module, for the voice chat (Slingshot loads it at run time: dm/slingshot/src/coop/voice.c).
# 7. The kit's coop-options.ini, once. Host and join from the game: the pause menu's Multiplayer tabs, the title's rows, or menu (F1) › Mods › Squadala!.
set -euo pipefail
here=$(cd "$(dirname "$0")/../../.." && pwd)                  # nxnative (this is dm/slingshot/tools)
SQ_URL=https://github.com/daurosk/oot-remake-multiplayer SQ_TAG=v0.11.12 SQ_COMMIT=8a99851f61330950995da3fe95c17e5bf7ba20ae
RR_URL=https://github.com/ootrando/ootrr                 RR_TAG=v0.8.2   RR_COMMIT=0fa64f44b5e689f39bcf98a151fc32cb019d538e
SRCDIR=$HOME/OcarinaNative/src
ROMFS=$HOME/OcarinaNative/game/updates/131072/romfs
PACK=0
for a in "$@"; do case $a in --pack) PACK=1 ;; *) ROMFS=$a ;; esac; done
APP="$HOME/Library/Application Support/Slingshot"
COOP=$APP/coop
MODS=$APP/mods
say() { printf '\n== %s\n' "$*"; }
# A release's source: its tag cloned once, and its commit checked (a moved tag stops here)
fetch() {   # url tag commit dir
    [[ -d $4/.git ]] || git clone -q -c advice.detachedHead=false --depth 1 --branch "$2" "$1" "$4"
    local got; got=$(git -C "$4" rev-parse HEAD)
    [[ $got == "$3" ]] || { echo "$4 is at $got, not $2 ($3): move it away and run this again"; exit 1; }
}

[[ -d $ROMFS/Pack && -d $ROMFS/System ]] || { echo "No game romfs at $ROMFS"; exit 1; }
mkdir -p "$SRCDIR" "$COOP" "$MODS"
eval "$(/opt/homebrew/bin/brew shellenv 2>/dev/null || true)"
command -v cmake >/dev/null || { say 'Installing CMake (Homebrew)'; brew install cmake; }
command -v cargo >/dev/null || { say 'Installing Rust (Homebrew), for OOTRR'; brew install rust; }
PY=python3
[[ -x $HOME/OcarinaNative/venv/bin/python3 ]] && PY=$HOME/OcarinaNative/venv/bin/python3
"$PY" -c 'import zstandard' 2>/dev/null || "$PY" -m pip install -q zstandard

if [[ -n ${SQUADALA_SRC:-} ]]; then
    SRC=$SQUADALA_SRC
    grep -q 'if(APPLE)' "$SRC/hookshot/CMakeLists.txt" || { echo "$SRC lacks the macOS changes (dm/slingshot/patches/squadala-macos.patch)"; exit 1; }
else
    say "Squadala! $SQ_TAG"
    SRC=$SRCDIR/squadala-$SQ_TAG
    fetch "$SQ_URL" "$SQ_TAG" "$SQ_COMMIT" "$SRC"
    PATCH=$here/dm/slingshot/patches/squadala-macos.patch
    if ! git -C "$SRC" apply -R --check "$PATCH" 2>/dev/null; then   # (not in as it is now: the tag's own files again, then the changes; an older setup's are replaced)
        git -C "$SRC" checkout -q "$SQ_COMMIT" -- $(sed -n 's#^+++ b/##p' "$PATCH")
        git -C "$SRC" apply "$PATCH" || { echo "$SRC: the macOS changes do not apply"; exit 1; }
    fi
fi
say 'Building the Squadala! module for macOS'
cmake -S "$SRC" -B "$SRC/build-mac" -DCMAKE_BUILD_TYPE=Release -DCOOP_BUILD_HOOKSHOT=ON -DCOOP_BUILD_TESTS=OFF -DCOOP_BUILD_PROGRAMS=OFF >/dev/null
cmake --build "$SRC/build-mac" --target hookshot_coop -j "$(sysctl -n hw.ncpu)"
cp "$SRC/build-mac/hookshot/hookshot_coop.dylib" "$COOP/hookshot_coop.dylib"
echo "module: $COOP/hookshot_coop.dylib"
# libopus for the voice chat: Slingshot loads it at run time (dm/slingshot/src/coop/voice.c), Homebrew's copied beside the module
OPUS=$(brew --prefix opus 2>/dev/null || true)
[[ -n $OPUS && -f $OPUS/lib/libopus.0.dylib ]] || { say 'Installing Opus (Homebrew), for the voice chat'; brew install opus; OPUS=$(brew --prefix opus); }
cp -L "$OPUS/lib/libopus.0.dylib" "$COOP/libopus.0.dylib"
echo "voice: $COOP/libopus.0.dylib"

say "OOTRR $RR_TAG (the randomizer)"
RR=$SRCDIR/ootrr-$RR_TAG
fetch "$RR_URL" "$RR_TAG" "$RR_COMMIT" "$RR"
OUT=$COOP/tools/ootrr
if [[ ! -x $OUT/ootrr || $(cat "$OUT/VERSION" 2>/dev/null) != "$RR_TAG $RR_COMMIT" ]]; then
    cargo build --release --locked --manifest-path "$RR/Cargo.toml"
    mkdir -p "$OUT"
    cp "$RR/target/release/ootrr" "$RR/LICENSE" "$RR/NOTICE" "$OUT/"
    echo "$RR_TAG $RR_COMMIT" > "$OUT/VERSION"
fi
"$OUT/ootrr" help >/dev/null && echo "ootrr: $OUT/ootrr ($RR_TAG)"
ln -sfn "$ROMFS" "$COOP/romfs"   # the game files OOTRR reads (the module's <game>/romfs); it never changes them

say "One mods folder for Slingshot and Squadala!"
if [[ -d $COOP/mods && ! -L $COOP/mods ]]; then   # (an earlier setup's own folder: what Squadala! made there moves over)
    for f in "$COOP/mods"/* "$COOP/mods"/coop-mods.*; do
        [[ -e $f || -L $f ]] || continue
        n=$(basename "$f"); [[ $n == contents ]] && continue
        [[ -e $MODS/$n ]] || mv "$f" "$MODS/$n"
    done
    rm -rf "$COOP/mods"
fi
ln -sfn "$MODS" "$COOP/mods"

# The pack: made again when it was made by another release or with other switches (coop-puppet/slingshot-pack.txt), as the kits make it again for theirs
PACK_STAMP="squadala $SQ_TAG --title-rows --pause-tab --no-characters-tab"
if [[ $PACK = 1 || ! -f $MODS/coop-puppet/coop_puppet.json || $(cat "$MODS/coop-puppet/slingshot-pack.txt" 2>/dev/null) != "$PACK_STAMP" ]]; then
    say 'Making the puppet pack from your game files (a few minutes)'
    "$PY" -B "$SRC/tools/puppet_pack/build.py" --romfs "$ROMFS" --mod-dir "$MODS/coop-puppet" --title-rows --pause-tab --no-characters-tab
    echo "$PACK_STAMP" > "$MODS/coop-puppet/slingshot-pack.txt"
fi
mkdir -p "$MODS/contents/0400486024DEC000"
ln -sfn ../../coop-puppet "$MODS/contents/0400486024DEC000/coop-puppet"   # where the module looks for its pack

# The pack tools Slingshot runs at each start (self_tunic.py, merge_tables.py), and the Python with zstandard that runs them
say "Squadala!'s pack tools, for Slingshot's starts"
rm -rf "$COOP/tools/puppet_pack"
mkdir -p "$COOP/tools"
rsync -a --exclude tests --exclude __pycache__ "$SRC/tools/puppet_pack/" "$COOP/tools/puppet_pack/"
command -v "$PY" >/dev/null && "$PY" -c 'import sys; print(sys.executable)' > "$COOP/python.txt"
echo "merge: $COOP/tools/puppet_pack/merge_tables.py with $(cat "$COOP/python.txt")"


[[ -f $COOP/coop-options.ini ]] || cp "$SRC/tools/release/kit/coop-options.ini" "$COOP/coop-options.ini"
say "Done. In the game: the pause menu's Multiplayer tabs (the - menu), the title's rows, or menu (F1) › Mods › Squadala!. Squadala!'s folder: $COOP"
