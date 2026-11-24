# Squadala! on the Mac

Slingshot is one more Squadala! client, so Mac players join games hosted on Windows or Linux and the other way round. The rules it follows:

- **The same module.** Slingshot runs Squadala!'s own module (`hookshot_coop.dylib`). It is built from the release's own source with a small macOS patch (`patches/squadala-macos.patch`). Every feature, page, rule and message is Squadala!'s.
- **The same pages.** The pause menu's own tabs (press −): Squadala!'s pack adds Multiplayer, Lobby and Mods beside the game's tabs, and the module draws them in the game, as on PC. The System page's Slingshot Settings button opens Slingshot's own menu, as Longshot Settings opens Longshot's. Slingshot's menu still has the module's tabs too (Mods › Squadala!), drawn in its own style. A new Squadala! release brings its new rows with it.
- **The same seeds.** The randomizer is the module's own, with OOTRR built from the tag Squadala! pins. OOTRR writes only RomFS files, so the Mac gets the same files as a PC.
- **Pinned.** `tools/squadala_setup.sh` clones Squadala! v0.11.12 (commit `8a99851`, protocol 16) and OOTRR v0.8.2 (commit `0fa64f4`, the one 0.11.12 ships), checks both commits, and builds them. Moving to a new Squadala! release moves both pins together.

Crossplay is part of Squadala!'s design (its `docs/crossplay.md`): the server refuses only a different protocol, game build or password, never a client.

## How the module is wired in

On Windows and Linux, Squadala! patches Longshot, the PC build. Slingshot gives the module the same things through nxnative:

- **Hooks** (`dm/games/0400486024DEC000/patches.py`): observation gates at the game's frame sites, the AI hold, world events and the title screen. Each sits on an instruction checked against the game build, never on a PC-relative one. STUBS entries add the puppet guard and the bow guard. Each wrapped function's first instruction becomes a branch to a small stub. The stub finds the bridge's entry in the frame-step state page, or runs the game's own code when co-op is off.
  - A gate can change registers but not where the game goes next. So LIFE_REVIVE (patch 0007), which may tell the game's revive check that a dead puppet has no revive, points the check at a small record of Slingshot's that answers "none", by changing the register the check's argument is made from.
  - The bow guard (patch 0038) is a stub of its own: a bow or slingshot worn by a Link with no Player component (a puppet) takes the bow update's own exit.
- **The bridge** (`src/coop/bridge.c`, `src/coop/call.S`) does the following:
  - It loads the module at the first frame hook and hands it the `HosCoopApi` table: memory reads and writes through fault-safe probes, game calls on the game's own stack, the log, the pad, keys and scratch.
  - It gives game code that loads from an address below 1 MB zero instead of a crash, as the module expects on non-Windows hosts.
  - A wrapped function's phases run on a big host stack, because game threads can have small stacks.
  - Co-op's per-present work (the module's main work: players, actors, network) runs on a thread of its own, which each present wakes. nxnative calls the present hook on its display link's thread just before the picture goes up, where that work would make pictures late. Longshot calls it inside the game's own `nvnQueuePresentTexture`, which has no such deadline.
  - Every 10 s it logs what co-op cost (`coop: cost`): the per-present work, and the time the hooks took on the game's threads, by kind.
- **Name tags' camera** (`nvn_mtl.m`): the draws into the scene's colour target hand the module the vertex stage's uniform buffer 3, which is NVN binding 0 (`S.ubo[0][0]`), as Longshot's `s.ubo[0][0]`. Once a frame has its camera, the rest of the frame's draws ask nothing.
- **Exports of the app** that the module finds by name, as on Linux: texture overrides, save folders and the save slot, and SDL's clipboard and modifier calls.
- **The front end** (`src/squadala.h`): the menu tabs, the drawing over the picture (chat, name tags, player list), keys, typing and the mouse in play.
- **The game's menus and camera** (`src/coop/menus.c`): three more gates, where Longshot calls its own host code in its lift. They are every menu screen's update, the pause menu's focus handler, and the head of the view routine's loop. From them Slingshot keeps which menu screens run, as Longshot's `host/oot_menu.c` does: the HUD screen, the panel's frame, whether the Map page is shown, and whether a game menu is in front. It exports that to the module as `hos_coop_menu_state`, and runs the HUD's own refresh when the module asks for it. The gameplay camera goes to the module's `hos_coop_view` first, for the Customize page's turn around your Link. Its timing is tighter than Longshot's 200 ms: a screen counts as stopped once it misses its update for 1.6 game frames, and the Map page counts as shown two frames after it opens (about how far the picture on screen trails the game). So the other players' marks come and go with the map.
- **The pause menu's tabs** (patch 0030): every menu screen's update also goes to the module (`hos_coop_game_screen`), which shows its pages over the System page and lights its tabs. The pack is built with `--pause-tab`. Squadala!'s own Characters tab needs a code patch of the game's (an IPS file), which Slingshot cannot apply yet, so the setup builds the pack without it (`--no-characters-tab`, in `patches/squadala-macos.patch`); the − menu keeps the game's tabs and co-op's three.
- **Slingshot Settings** (patch 0037): the module asks for the host's own menu (`HOS_COOP_MENU_WANT_LONGSHOT`, −3) when System's button is pressed, and Slingshot opens its menu. The button's label is the pack's (`tools/puppet_pack/strings/en.txt` in the macOS patch).
- **Game addresses.** The module now takes every game address from where the game is loaded, so it needs no change for that.

## Parity with Longshot's co-op patches

| Patch | What it gives Squadala! | On Slingshot |
|---|---|---|
| 0001 bridge | Frame hook, AI hold, module loader, present call, drawing over the picture, keys and mouse to the module first, name tags' camera | Done |
| 0002 puppets | Null reads give 0; puppet guard | Done |
| 0003 / 0005 world hooks | Pause counters, flag applier, time, events; wraps | Done |
| 0004 title menu | Multiplayer rows on the title screen; the pad to the module first | Done |
| 0007 actor events | Creates, deletes, Life step, shots, and LIFE_REVIVE (a dead puppet answers no revive) | Done |
| 0008 host save | A guest plays from `<save>-coop` with the host's save | Done (a new save transaction) |
| 0009 voice | Proximity voice chat in the game's mix | Done (`src/coop/voice.c`, Homebrew's libopus) |
| 0011 mods list | `coop-mods.list` order and switches, its `default off` line (Original game); `coop-mods.applied`; co-op's own packs always on; a mod's code patches only as built | Done: one shared mods folder, Longshot's mod order. A mod with code patches (`exefs/*.ips`, `*.pchtxt`: Character Select) stays off, since Slingshot cannot build code mods in yet |
| 0012 menu tabs | The module's tabs in the menu | Done |
| 0013 boss wraps | Brain and action verbs | Done |
| 0014 sound wrap | Other Links' sounds | Done |
| 0015 pad select | Which controller this game uses (two games on one computer) | Not yet |
| 0016 local pads | Split-screen players 2–4 (not played in the game yet upstream) | Not yet |
| 0017 player input | Player's pad reads wrapped | Done |
| 0018 map state | Players on the Map page; minimap icons behind menus; HUD refresh; the screens that ran in the last 100 ms (the player list under the HUD) | Done (`src/coop/menus.c`, from Longshot's `host/oot_menu.c`) |
| 0019 menu picture / view | The Customize page's Link picture and its camera | Done (the camera: `src/coop/menus.c`, from Longshot's `hos_oot_free_view`) |
| 0020 texture overrides | Your own Link's tunic and gear colours | Done |
| 0022 typing | Keyboard to the module's text fields | Done (⌘V pastes) |
| 0024 save-load wraps | Every save load to the module | Done |
| 0025 save slot | The slot the game opened | Done |
| 0026 Discord status | Status line | No Discord on Slingshot. The status line shows as the Squadala! row's value |
| 0027 crash report | Longshot's reports kept clean | Not needed |
| 0028 skip title | Title-skip hold | Not needed (Slingshot has no title skip) |
| 0029 node starts | A pushed block's start (NODE_START) | Done |
| 0030 pause tab | Every menu screen's update to the module: the pause menu's Multiplayer, Lobby and Mods tabs | Done. The Windows mouse on the tab strip is not needed: the module's own mouse handling covers the Mac |
| 0031 menu column | An opaque column; the page arrow after an action's value | Done: the page arrow after a value (Slingshot draws its own column) |
| 0032 settings | Longshot's menu rows listed to the module | Not needed: Slingshot Settings opens Slingshot's own menu |
| 0033 SDL audio once | A Windows and Linux voice crash in Longshot's SDL audio | Not needed (Slingshot's microphones are its own) |
| 0035 mod code context | Registers a code mod's routines read | Not needed until Slingshot builds code mods in |
| 0036 boss orders wrap | Kotake and Koume take their battle manager's orders | Done |
| 0037 menu want | System's button opens the host's own menu | Done: Slingshot Settings |
| 0038 puppet bow guard | A puppet wearing a bow or slingshot | Done |
| 0039 menu pads behind | Longshot's menu takes the pads with the window behind | Not needed: on the Mac the game takes controllers only while its window is in front, so the button is pressed there, where Slingshot's menu reads them too |

At each start, before the game reads a file, Slingshot runs Squadala!'s own start steps as the PC kits do, with the Python and the tools the setup put in `coop` (`src/mods.h`):

- `tools/puppet_pack/self_tunic.py` puts your Link's tunic files in the pack. With a pack that has `coop_look/green.bin`, that is green's files, and the module paints your colour on them in the game.
- `tools/puppet_pack/merge_tables.py` merges the size tables, puts the puppets' rows into other mods' actor tables, writes the one-time keys file, and keeps the pack's menu files out where a mod's table cannot hold them. It sees the mods through `coop/merge-view`: a link for each mod, as Squadala! names it, and a mods list that says what Slingshot gives the game this start.
- Then Link Skins' table goes over every mod and skin (`src/size_table.c`) and is marked with `coop-merged.txt` when it holds the puppet pack's entries.

Without the Python or the tools, Slingshot does the size-table part itself (`src/size_table.c`), as before.

## Testing on one Mac

`tools/squadala_test.sh` starts Squadala!'s own server on this Mac and turns on the module's test puppet: a second Link stands beside yours. Run the same script with `--off` afterwards.

## Notes

- Squadala!'s address-shape checks assume pointers between 4 GB and 1 TB. nxnative maps the game there with host ASLR.
- Null reads cost a signal each: fine for a few per frame, slow if there are hundreds.
- Licences: the module stays GPL-3.0 and is built from its own source as a separate library. Slingshot's bridge declares the table layouts itself.
