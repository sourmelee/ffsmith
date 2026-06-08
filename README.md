# FFSmith

Clean-room reimplementation of the **Final Fantasy Dimensions / Final Fantasy Legends** engine, in C++17. The working name nods to the engine's own `Mtx` ("Matrix") middleware.

Companion to the Python **FFD/FFL Toolkit** (`../Python/`): the toolkit bakes verified assets that FFSmith loads, and the two are developed together. The toolkit's parse output is FFSmith's source of truth and its golden test data. See `docs/ASSET_PIPELINE.md`.

## Status

- **M0 — host + main loop: ✅ done.** SDL2 window, fixed-timestep logic loop, input edge-detection, headless `--frames` mode. Verified compiling clean (`-Wall -Wextra -Wpedantic`) and running headless (120 ticks, clean exit).
- **M1 — static map render: ✅ done.** Toolkit bakes `.ffmap` + `.tex`; FFSmith loads and composes them. Verified **byte-identical** (100% exact pixels, max channel diff 0) to the toolkit render on a 1-layer map (g0_p0_m101) and a 2-layer / dual-slot map (g0_p0_m501).
- **M2 — field movement + camera: ✅ done.** Walk a player (arrows/WASD) around a baked map; smooth one-tile-stepped movement, facing, and a follow-camera clamped to map bounds. Verified by deterministic headless `--walk` traces (movement, bounds-block, facing).
- **M2.1 — wall collision: ✅ done.** Decoded `capk.dat` (per-tileset chip attributes); the toolkit bakes a per-cell passability grid (FFM1) and `Field::isSolid` blocks walls/objects. Verified: solids overlay exactly on walls & furniture; the player stops at interior walls.
- **M3 (core) — event VM + NPCs + dialogue: ✅ done.** The baker emits structured events (FFM2); the engine runs an event-script VM, places NPCs (solid), and face-to-talk (Z/Enter) opens a placeholder dialogue box from the script's messages. Verified headless (`--events`): NPC at (4,6) -> messages 170-175.
- **M3b (sprites) — real field sprites, facing + walk animation: ✅ done.** Player + NPCs render as actual `fldchr` sprites (feet-aligned, transparent), facing the right way and **animating while walking**. Layout (`field_anm` character template, confirmed on fldchr1): 48×48 cells, origin (1,1), pitch 50 — **rows = facing** (Down=y1, Up=y51, Left=y101, **Right = Left flipped**), **cols = frame** (idle=x1, walkA=x51, walkB=x101). Verified in-engine via `--fieldshot --face`.
- **M3b (triggers) — step-on triggers: ✅ done.** Walking onto an invisible scripted trigger event (`img<=0`, boot 2-8 per the engine's `GetEventBootCondition` switch: 1=talk, 6=0x1b, 7=0x1c range-always, ...) auto-runs its script via the VM (e.g. g0_p0_m200 (1,1) → dialogue msg 51). Verified headless.
- **M3b (warps) — cross-map warps (script `MapChange`): ✅ done.** A `MapChange` (0x41) in an event script warps the player: the VM extracts the destination (map id + dest x/y/dir), the engine resolves the map id to a bundle map (`find_map_key`), loads & composes it, and repositions the player. The loop runs a frame at a time (`Host::frame`) so maps swap live. Verified headless: a step-trigger `MapChange`→map 888 @(3,3) loads the new 7×7 map and drops the player at (3,3); an absent target id fails cleanly.
- **M3b (doors) — door & map-edge warps: ✅ done.** Real doors/stairs warp via the script-variable idiom (`0x6B` BulkSetVars `sub2` sets var0=dest map / var2=x / var3=y / var4=dir; `0x66` SetEntityAction action `0x04` executes it) — not a header field as first assumed, nor the rare `MapChange`. The VM decodes it; the engine loads + repositions. Verified on real data: m501's exit door → town m500 @(37,28) and m500's building door → interior m501 @(6,10) (a coherent round trip); 4507/4514 warp records across the game resolve in-bounds.
- **M3b (dialogue) — real on-screen text: ✅ done.** SetMessage shows actual words in a bordered, word-wrapped, `\n`-aware dialogue box. Field dialogue is **per-area**: the engine loads `msg{N}.msd` (`ReadStoryMessageData`→`SetMessageList`→`FieldClass+0x380`), and maps select the bank by **group** (16 groups ↔ 16 banks). msg-file format: count + messages × 6 languages × 2 slots (text + speaker name); **English text = msg×12+2**. The toolkit bakes `text/msg{group}.bin` + a font atlas; the engine loads the right bank per map (reloading on warp). Verified via `--fieldshot --open-msg`: m501's NPC (msg 170) shows “Hey, Hero…”, m500's cutscene wraps across lines. *(An earlier cut used system_message.msd §4 — a coincidentally-coherent but wrong source; corrected to the real `msg{N}` banks.)*
- **M4 (state machine) — title → field → menu: ✅ done.** A top-level mode machine (mirroring `GameClass::MainFunc`: Boot/Title/Field/Battle, with Menu/Dialog as **overlays**) boots to a real title screen (`TitleLogo`), enters the field on **Start/Z**, and opens a cursor menu (Item/Equip/Status/Save/Quit) over the **paused** field (**Enter** open, **X** close; Quit exits). `ChangeMainFunc` = current/next-mode + scene Init/Exec/End. Verified via `--title` / `--menu` screenshots. Battle (mode 4) is stubbed for M6.
- **M5 (menu pages) — Item / Equip / Status: ✅ (core).** The field menu's pages show **real decoded data**: a scrollable 500-item list with descriptions (**Item**), and each of the 21 roster characters with their actual equipment resolved to item names (**Equip**) and stat descriptions (**Status**) — e.g. Sol with Orichalcum (ATK 23). Baked from `system_message.msd` + `boot_data` + `chara_set.dat`; navigated with the cursor (Item: ↑↓ + L/R ±10; Equip/Status: ←→ cycle character; X back). Verified by screenshot (`--menupage 1|2|3`). Save is a stub (needs save state); Quit exits.
- **M6 (battle) — turn-based scaffold: ✅ (step 1).** A real battle mode: a battle scene (real **battle background**) with a **real enemy from the monster table** (name + HP/attack/defense, e.g. Goblin 21/10/3), the **named party** (Sol/Aigis/Dusk/Sarah from `chara_set`) with HP bars, and a turn loop — **Attack / Defend / Run**, damage calc, enemy turns, win/lose. **M6.2:** battles are now party-vs-**group** (1-3 real enemies, difficulty-banded around the lead so a Goblin pack never includes a Mud Golem; duplicate names auto-suffix "Goblin A/B"), with **target selection** on Attack and per-enemy turns. Start with the field hotkey **B** (random encounter) or `--battle N`; verified headless with `--battlesim N` (Goblin → Victory in 5 steps). *Scaffold: party stats are placeholder — real stats, ATB, abilities/magic, the exact damage formula, and encounters/rewards are M6 cont.*
- **Character stats decoded: ✅.** `chara_set.dat` per-character **STR / SPD / VIT / INT / MND** (+ job, level) are decoded (engine-verified via `ReadStartData`/`MEMBER_STATUS`) and shown on the **Status** page (e.g. Sol Lv3 STR20 SPD5); battle damage now uses **real STR** (Sol hits for ~17, Aigis ~8). HP/MP are now **real** too (decoded growth table, boot_data §8 — Sol L3 HP70/MP23, shown on Status + used as battle max-HP), and the battle runs an **ATB turn order by SPD** (the gauge fills proportional to SPD, so Sarah SPD14 acts ~2.5x as often as Sol SPD5). Battles also have a **Magic** command (real spell table — Cure/Fire/Blizzard/…; each member knows spells by INT/MND, gated by MP; damage scales with INT, heals with MND). Damage uses the **decoded `CalcPhysicAttackDmg` core** wired to real BTLACT inputs (see `docs/BTLACT_MAP.md`): `(weaponATK<<6 + spread − defense<<6) × (3·STR + level) / 1024`, with weapon ATK / armor DEF parsed from the item table — so it scales with **equipment + STR + level** (Aigis w/ Graham's Sword lv10 → ~110, Dusk w/ Short Sword → ~11, unarmed Sarah → 1). Magic scales with INT vs a magic-defense proxy. Remaining (full exact-match): the job-derived attack stat for `A`, the crit / element / race / status / hit-count modifiers, status effects, per-job HP%/MP%, encounter tables + rewards.
- **M7 (save/load) — field-state persistence: ✅.** The game saves to `save.bin` (15 KB/slot via `GameClass::SaveGameData`/`SetGameData` — full party MEMBER_STATUS, inventory, gil, position, flags). FFSmith persists its current mutable state — **current map + player tile + facing + sprite** — to `save.dat` (`FSAV`): **Save** in the field menu or **F5**; **F9** or `--load` (Continue) restores it on boot; `--save` writes a one-shot. Verified round-trip (save m501@(6,11) → reload resumes there). Full party/inventory/flag save grows as those become persistent.
- **Next:** flesh out **M6** (real party stats from jobs/level, ATB turn order, abilities/magic, exact damage formula, encounter tables + rewards), then **M7** save/load.

Full plan and the reverse-engineering map: **`ENGINE_RE_ROADMAP.md`**.

## Build

Requires a C++17 compiler, CMake ≥ 3.16, and SDL2 (dev headers + lib).

```sh
cmake -S . -B build
cmake --build build
./build/ffsmith
```

On Windows: install SDL2 (e.g. `vcpkg install sdl2`, or the SDL2 development package) and configure CMake with the vcpkg toolchain or `-DSDL2_DIR=...` as usual.

## Run / controls

```sh
./build/ffsmith                 # windowed, runs until you quit
./build/ffsmith --frames 120    # headless/CI: run 120 ticks then exit
./build/ffsmith --scale 4 --hz 60
./build/ffsmith --help
```

Arrows / WASD = move · Z confirm · X cancel · Enter/Tab menu · F1-F4 debug · F5 save / F9 load · Esc quit.

## Running a baked map

First bake a bundle with the toolkit (creates `maps/` + `tex/` + `manifest.json`):

```sh
python ffd_toolkit.py --bake-ffsmith out_bundle --proper ../Android/proper_obb
```

Then point FFSmith at it. If `maps/` and `tex/` sit next to `ffsmith.exe`, the
bundle defaults to the executable's folder, so just pass a map:

```sh
ffsmith --map g0_p0_m501            # bundle = exe folder by default
ffsmith --bundle out_bundle --map g0_p0_m501
```

**Map keys** are `g{group}_p{pack}_m{id}` (note the `p`): e.g. `g0_p0_m501`, not
`g0_0_m501`. List what you have with `ls maps` / `dir maps` and drop the
`.ffmap` suffix. Good first maps: `g0_p0_m501` (small interior), `g0_p0_m101`.

## Debug launcher (default boot)

By default FFSmith boots into a **debug launcher** (no `--map` needed — it lists the
bundle's maps): choose the **map**, the **character** (player `fldchr` sprite),
the **spawn tile** (X/Y) and **facing**, toggle **No-clip**, **Collision** overlay and the
**HUD**, and set **Scale** (zoom); **START** drops you into the field.

```sh
ffsmith --bundle out_bundle            # -> debug launcher
ffsmith --bundle out_bundle --title    # -> title screen -> launcher
ffsmith --bundle out_bundle --map g0_p0_m501   # preselects that map in the launcher
```

In the launcher: **arrows** move the cursor / change a value, **L/R (Q/E)** jump by
10 through long map & character lists, **Z** toggles or activates **START**.

In the field, debug hotkeys: **F1** back to the launcher, **F2** no-clip, **F3**
collision overlay, **F4** HUD, **-/+** zoom, **B** test battle. The HUD shows `map (x,y) facing`.

A window opens; **arrows / WASD** walk the player (yellow marker), the camera
follows, **Esc** quits. Big maps make big windows — use `--scale 1` or `2`.

Headless options (no window): `--shot out.tex` writes the composed map; `--walk URDL...` prints the player's tile path for a scripted move sequence.

On Windows, keep `SDL2.dll` next to `ffsmith.exe`.

---

## Source layout

```
src/host/   window, renderer, input, fixed-timestep loop         (M0 ✅)
src/mtx/    reimplemented Mtx framework contracts                 (later)
src/data/   asset loaders for the baked bundle                    (M1)
src/field/  field engine: movement / collision / camera / events (M2+)
src/game/   state machine, scenes, save                           (later)
docs/       ASSET_PIPELINE.md + per-subsystem specs
```

**Clean-room note:** FFSmith ships no game assets. It loads content you bake locally from your own `main.obb` via the toolkit.

## Credits

FFSmith is **co-authored using Claude Cowork** (Anthropic), developed alongside the
FFD/FFL Toolkit (`../Python`) as a clean-room reimplementation. See `ENGINE_RE_ROADMAP.md` for the design and `docs/ASSET_PIPELINE.md` for the
toolkit↔engine asset contract.
