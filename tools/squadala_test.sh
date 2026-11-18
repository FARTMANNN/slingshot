#!/bin/bash
# Slingshot: co-op test on this Mac alone, no second PC. Run it after dm/slingshot/tools/squadala_setup.sh:
#   bash dm/slingshot/tools/squadala_test.sh          builds co-op's own server, points the game at it, turns on co-op's test Link, runs the server
#   bash dm/slingshot/tools/squadala_test.sh --off    puts coop.ini and coop-options.ini back as they were
# The server (co-op's coop_server, only reachable from this Mac) runs in this window until Ctrl+C. Start the game and load a save: about
# 3 seconds after you arrive, co-op makes a second Link 2 steps beside you through the game's own functions, the way it makes another
# player's (LICHII_COOP_TEST_PUPPET), and he copies your moves (LICHII_COOP_DEBUG_POSE). That is the puppet pack, co-op's calls into the
# game and the null reads at work, without a second player.
set -euo pipefail
SRC=${SQUADALA_SRC:-$HOME/OcarinaNative/src/squadala-v0.11.4}   # (dm/slingshot/tools/squadala_setup.sh puts it there)
COOP="$HOME/Library/Application Support/Slingshot/coop"
INI=$COOP/coop.ini OPT=$COOP/coop-options.ini
[[ -f $OPT && -f $COOP/hookshot_coop.dylib ]] || { echo "Run dm/slingshot/tools/squadala_setup.sh first"; exit 1; }
if [[ ${1:-} = --off ]]; then
    if [[ -f $INI.before-test ]]; then mv "$INI.before-test" "$INI"; else rm -f "$INI"; fi   # (no coop.ini before: none after)
    sed -i '' '/^# slingshot test$/d; /^LICHII_COOP_TEST_PUPPET=1$/d; /^LICHII_COOP_DEBUG_POSE=1$/d' "$OPT"
    echo "Test settings removed."; exit 0
fi
command -v cmake >/dev/null || eval "$(/opt/homebrew/bin/brew shellenv)"
echo "== Building co-op's server"
cmake --build "$SRC/build-mac" --target coop_server -j "$(sysctl -n hw.ncpu)" >/dev/null
[[ -f $INI.before-test || ! -f $INI ]] || cp "$INI" "$INI.before-test"
cat > "$INI" <<'INI'
# Slingshot co-op test (dm/slingshot/tools/squadala_test.sh --off puts the one before back)
server = 127.0.0.1
port = 27510
password = slingshot-test
name = Slingshot
INI
grep -q '^LICHII_COOP_TEST_PUPPET=1$' "$OPT" || printf '# slingshot test\nLICHII_COOP_TEST_PUPPET=1\nLICHII_COOP_DEBUG_POSE=1\n' >> "$OPT"
echo "== Co-op server on this Mac (Ctrl+C stops it). Now start the game and load a save."
exec "$SRC/build-mac/server/coop_server" --bind 127.0.0.1 --port 27510 --password slingshot-test
