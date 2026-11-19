# Slingshot

Slingshot is a Mac front end for the *Ocarina of Time* remake (Switch 2, version 1.1.1, build `e0f3ccfaf50065ae`). The game runs natively on Apple silicon through [nxnative](https://github.com/seedds/nxnative). Slingshot adds:

- an in-game menu in the game's own style (graphics, controls, mods, FPS counter);
- mods, including Link Skins (skins for Link, changed while you play);
- [Squadala!](https://github.com/daurosk/oot-remake-multiplayer) online co-op. It is the same module as on Windows and Linux, built from its own source, so Mac players join the same games. Its randomizer ([OOTRR](https://github.com/REALAZK/ootrr)) gives the same seeds.

**Status:** testing.

**Game files:** none are included, downloaded or made available here. You bring your own dump of the game and its update.

## What you need

- An Apple silicon Mac and the Xcode command line tools.
- Your own dump of the game, version 1.1.1 (nxnative's README says how to give it to the build).
- For co-op: [Homebrew](https://brew.sh). The setup script installs CMake, Rust and Opus with it.

## Install

Slingshot lives inside an nxnative checkout, at `dm/slingshot`:

```sh
git clone https://github.com/seedds/nxnative.git
cd nxnative
git checkout e4b0d9b75d39c41c10fdcfec2f0b070df496d7e5   # the nxnative commit Slingshot is made for
git clone https://github.com/FARTMANNN/slingshot.git dm/slingshot
bash dm/slingshot/tools/install.sh
```

`install.sh` applies Slingshot's hooks to nxnative's files (`patches/nxnative.patch`) and keeps `dm/slingshot` out of nxnative's own git status. Then build the app the way nxnative does:

```sh
python3 scripts/build_ipa_from_nsp.py <your game folder> --platform mac-app
```

These two steps are optional:

- **The menu's look and the app icon**, made from your own game files (do this once):
  `python3 dm/slingshot/tools/ui_assets.py <game romfs> ~/Library/Application\ Support/Slingshot/ui`
- **Squadala! co-op and its randomizer** (do this once, and again when Slingshot moves to a new Squadala! release):
  `bash dm/slingshot/tools/squadala_setup.sh <game romfs>`

## Playing

- **The menu:** press F1 or Esc, or tap Home on a controller.
- **Mods:** each mod is a folder in `~/Library/Application Support/Slingshot/mods` with a `romfs` folder inside. You can also use the emulator layout (`contents/0400486024DEC000/<mod>/romfs`). Turn mods on and off on the menu's Mods page.
- **Link Skins:** each skin is in `mods/link-skins/skins/<name>/`, with `romfs/` and a `skin.json`. Pick one on the Mods page. The skin shows when you load your save again or Link changes age.
- **Co-op:** open the menu, then Mods › Squadala! › Multiplayer, or use the Multiplayer rows on the title screen. To host, to join, and to make randomizer seeds, it works as on PC.

Slingshot keeps its files in `~/Library/Application Support/Slingshot`:

| Folder | What it holds |
|---|---|
| `ui` | The menu's look. |
| `mods` | Your mods. Squadala! uses the same folder. |
| `coop` | Squadala!'s module, its settings, OOTRR, libopus, and links to the game files and the mods. |

## Repository layout

| Path | What it is |
|---|---|
| `src/menu.h` | The in-game menu. Rows are data (`sm_row`), and the look comes from the game's own fonts and textures. |
| `src/mods.h` | The Mods page and the mod loader. It finds mods the way Longshot does, orders them the same way, lets you wear one skin at a time, and reads Squadala!'s mod list. |
| `src/size_table.c` | The game's resource size table over every mod and skin, made at each start. It also does what the PC kit's start does: it marks or merges the table so the other players' puppets can load. |
| `src/squadala.h` | Squadala! in the menu (its own tabs, drawn in Slingshot's style), its drawing over the picture, keys, the mouse and typing in play. |
| `src/coop/bridge.c` | The bridge to Squadala!'s module (`hookshot_coop.dylib`): loading it, game hooks, memory access, game calls, null reads, the pad, keys. |
| `src/coop/call.S` | The bridge's assembly: game calls on the game's stack, memory probes, the entry of wrapped game functions. |
| `src/coop/textures.c` | Texture overrides, so your Link's colours change live. |
| `src/coop/voice.c` | Voice chat: the microphone, libopus and the game's mix, handed to the module. |
| `src/coop/menus.c` | The game's menus and camera for the module: which menu screens run (other players on the Map page, the minimap's icons hidden behind menus) and the Customize page's camera around your Link. Ported from Longshot's own code. |
| `src/third_party/zstd` | The decoding half of zstd 1.5.7 (BSD), used to read size tables. |
| `tools/install.sh` | Applies Slingshot's hooks to an nxnative checkout. |
| `tools/make_patch.sh` | For developers: writes `patches/nxnative.patch` again from your nxnative checkout. |
| `tools/squadala_setup.sh` | Clones Squadala! and OOTRR at their pinned release commits, builds both for macOS, makes the puppet pack, and links the shared mods folder. |
| `tools/squadala_test.sh` | A co-op test on one Mac: Squadala!'s own server on this Mac, and its test Link beside you. `--off` puts things back. |
| `tools/mods_table.py` | Link Skins' size-table rules as a script, for checking `size_table.c`. Both make the same table. |
| `tools/ui_assets.py` | Makes the menu's look and the app icon from your own game files. |
| `patches/nxnative.patch` | Slingshot's small hooks in nxnative's own files (listed below). |
| `patches/squadala-macos.patch` | Slingshot's small macOS changes to Squadala!'s source (the setup applies them): a `.dylib` build, the Mac clipboard, two compile fixes, SDL calls that Slingshot answers, and the title screen's and Map page's game addresses taken from where the game is loaded. |
| `docs/squadala.md` | How Squadala! runs on the Mac, and how each Longshot co-op patch maps to Slingshot. |

## Hooks in nxnative's own files

Each hook is commented where it is. `patches/nxnative.patch` holds them all.

| File | What Slingshot adds there |
|---|---|
| `dm/host/front_mac.m` | Includes `src/menu.h`. The key monitor gives Squadala! its keys first (chat box, typing). Closing the window stops co-op first, and losing focus lets go of co-op's keys. |
| `dm/host/home_quit.h` | A tap of Home opens the menu. |
| `dm/host/hle_fs.c` | The mods' RomFS overlay folders (`dm_rom_overlay_set`), and Squadala!'s save exports (a guest's save folder, the slot the game opened). |
| `dm/host/hle_hid.c` | Player 1's pad goes through Squadala! first, for its title menu. |
| `dm/host/hle_audren.c` | A hook that adds sound into each frame of the game's mix (`dm_audio_mix_hook`), for voice chat. |
| `dm/host/dm.h` | The number of the co-op observation gate (`OBS_COOP`). |
| `dm/host/Makefile` | Builds `src/` and links CoreText. |
| `dm/gfx/nvn/hle_nvn.c`, `nvn_obj.h` | Tells `src/coop/textures.c` which textures the game makes and finalizes. |
| `dm/gfx/nvn/nvn_mtl.m` | Calls co-op at each present, draws an overridden texture with its overrides, and hands the game's camera to Squadala!'s name tags. |
| `dm/mac/pack_app.py` | The microphone line in the app's Info.plist, for voice chat. |
| `dm/tools/patch_nso.py` | Gives a game's stub code the frame-step state page, where the wrapped functions find the bridge. |
| `dm/games/0400486024DEC000/patches.py` | The game's co-op hooks: observation gates (frame, world events, title screen, menu screens, camera), the puppet guard and the wrapped functions. Each is checked against the game's own instructions. |

## Troubleshooting

The log is `~/Library/Logs/nxnative/0400486024DEC000.log`, and the run before it is in `….prev.log`. Slingshot's lines start with `slingshot:`, and Squadala!'s with `coop:`.

| You see | It means |
|---|---|
| `slingshot: mods from …: N skins, N other mods, co-op's puppet pack` | The mods were found at start. |
| `slingshot: size table … made` or `… unchanged` | The size table over the mods is ready. A `[RED]` line says what could not be read. |
| `slingshot: size table: Link Skins' table holds every co-op puppet entry` | Squadala! will show the other players while skins are installed. |
| `slingshot: size tables: the game reads X's; merged in …` | A mod with its own size table (a seed from OOTRR's site, say) got the puppets' entries, as the PC kit does. |
| `coop: … made without the co-op puppets' entries: no puppet this run` | The table the game reads lacks the puppets, so other players show as fairies. Check the `slingshot: size table` lines above it. |
| `slingshot: Squadala!: your Link's tunic files: <colour>` | Your Link's colour for this start. |
| `coop: no co-op module at …: co-op off` | Squadala! is not installed. Run `tools/squadala_setup.sh`. |
| `coop: world hooks on (level 4): 6 events, 14 wrapped game functions` | All of co-op's game hooks are in. |
| `coop voice: on …`, or `coop voice: [RED] no libopus …` | Voice chat is ready, or Opus is missing (run the setup again). |
| `coop: title: the title's own rows … their texts could not be set` | The title screen's Multiplayer rows have no text. Run `tools/squadala_setup.sh` again, so the module is built with the current macOS patch. |
| `coop: … this Longshot has no …` | Slingshot does not have that feature yet. `docs/squadala.md` says which features are missing. |
| `coop: Hookshot hands co-op the game's camera (… scene): name tags on` | The other players' name tags have the game's camera. |
| `slingshot: power: …, Low Power Mode on …` or `thermal state serious` | macOS is slowing the chip down. The game can't hold 60 fps until you plug in, turn Low Power Mode off, or let the Mac cool. The menu's Graphics page shows the same under Power. |
| `coop: cost over N presents: …` | What co-op takes each frame, every 10 s: its main work on its own thread (p50, p95, max), then what its hooks take on the game's threads. Attach these lines to a frame-rate report. |
| `slingshot: Squadala!'s overlay over the last 10 s: up N% …` | How often the drawing over the picture (chat, name tags, map marks) was up. |

**Low frame rate?** Check these first: the Power row on the menu's Graphics page (Low Power Mode or heat), a window instead of full screen (the Display row), and handheld instead of docked. The `[nvn] frame step` lines say when and why the game dropped to 30 or 20 fps: `GPU 1.6` means the GPU took 1.6 frames of time, and a `cost` far above the `GPU` figure means the CPU side was slow. In that case turn Dynamic resolution off in the launcher. The game lowers its resolution whenever its frames run long, whatever the cause, so it can sit at 854x480 while the GPU has time to spare.

`SLINGSHOT_COOP=0` starts the game without Squadala!. `HOS_COOP_NULL_READS=0` turns off co-op's null-read handling, to rule it out when the game crashes. `HOS_COOP_VOICE=0` keeps voice chat off.

## Credits and licences

- **nxnative** (MIT) runs the game. Slingshot is made for it, and `patches/nxnative.patch` changes its files under its licence (see `THIRD_PARTY.md`).
- **Squadala!** by daurosk and its contributors (GPL-3.0). The setup builds it from its own source, and Slingshot loads it as a separate program. `patches/squadala-macos.patch` is a change to its source and is GPL-3.0 too.
- **OOTRR** by REALAZK (GPL-3.0-or-later). The setup builds it from its own source.
- **zstd** by Meta Platforms (BSD), in `src/third_party/zstd`.
- Slingshot's own code is MIT (`LICENSE`).

Slingshot is a fan project. It is not affiliated with or endorsed by Nintendo, and the game and its files belong to their owners.
