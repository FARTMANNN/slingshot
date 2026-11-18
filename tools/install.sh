#!/bin/bash
# Slingshot's install: puts Slingshot's hooks into the nxnative checkout this folder sits in (dm/slingshot), and keeps dm/slingshot out of
# nxnative's own git status. Run it from anywhere; run it again after updating either one (it says when the hooks are in already).
#   git clone https://github.com/seedds/nxnative.git && cd nxnative && git checkout <NX_COMMIT below>
#   git clone https://github.com/FARTMANNN/slingshot.git dm/slingshot
#   bash dm/slingshot/tools/install.sh
set -euo pipefail
NX_COMMIT=e4b0d9b75d39c41c10fdcfec2f0b070df496d7e5   # the nxnative commit patches/nxnative.patch is made against
here=$(cd "$(dirname "$0")/.." && pwd)                 # dm/slingshot
nx=$(cd "$here/../.." && pwd)                          # nxnative
say() { printf '\n== %s\n' "$*"; }

[[ -d $nx/.git && -f $nx/dm/host/front_mac.m ]] || { echo "Slingshot must be at dm/slingshot inside an nxnative checkout (found $here)"; exit 1; }
at=$(git -C "$nx" rev-parse HEAD)
[[ $at == "$NX_COMMIT" ]] || echo "Note: nxnative is at $at, not $NX_COMMIT, the commit Slingshot is made for. The hooks may not apply."

patch=$here/patches/nxnative.patch
if git -C "$nx" apply --reverse --check "$patch" 2>/dev/null; then
    say "Slingshot's hooks are in nxnative already"
elif git -C "$nx" apply --check "$patch" 2>/dev/null; then
    git -C "$nx" apply "$patch"
    say "Slingshot's hooks are in nxnative now ($(git -C "$nx" diff --stat | tail -1))"
else
    echo "patches/nxnative.patch does not apply to this nxnative. Check out $NX_COMMIT, or run tools/make_patch.sh in a checkout that has the hooks."
    exit 1
fi

exclude=$nx/.git/info/exclude
grep -qx '/dm/slingshot/' "$exclude" 2>/dev/null || { mkdir -p "$(dirname "$exclude")"; echo '/dm/slingshot/' >> "$exclude"; }
say 'Next'
echo "Build the app as nxnative does:  python3 scripts/build_ipa_from_nsp.py <your game folder> --platform mac-app"
echo "The menu's look (once):          python3 dm/slingshot/tools/ui_assets.py <game romfs> ~/Library/Application\\ Support/Slingshot/ui"
echo "Squadala! co-op (once):          bash dm/slingshot/tools/squadala_setup.sh <game romfs>"
