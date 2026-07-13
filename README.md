# FFSmith

[![version](https://img.shields.io/badge/version-0.3.0-blue.svg)](CHANGELOG.md)

Clean-room reimplementation of the **Final Fantasy Dimensions / Final Fantasy Legends** engine, in C++17. The working name nods to the engine's own `Mtx` ("Matrix") middleware.

Companion to the Python **FFD/FFL Toolkit** (`../Python/`): the toolkit bakes verified assets that FFSmith loads, and the two are developed together. The toolkit's parse output is FFSmith's source of truth and its golden test data. See `docs/ASSET_PIPELINE.md`.

## Versioning

FFSmith is versioned with [Semantic Versioning](https://semver.org/), starting at **0.1.0**, on the same conventions as the Toolkit. The canonical version is `FFSMITH_VERSION` in [`src/version.h`](src/version.h); `CMakeLists.txt` `project(... VERSION ...)` mirrors it. Changes are recorded in [`CHANGELOG.md`](CHANGELOG.md) (Keep a Changelog format); PATCH bumps happen freely, MINOR/MAJOR bumps are confirmed first. The badge above and this section track the *released* version — for live, in-progress engine state see [`docs/architecture/ffsmith_status.md`](docs/architecture/ffsmith_status.md).

## Status

- **M0 — host + main loop: ✅ done.** SDL2 window, fixed-timestep logic loop, input edge-detection, headless `--frames` mode. Verified compiling clean (`-Wall -Wextra -Wpedantic`) and running headless (120 ticks, clean exit).
- **M1 — static map render: ✅ done.** Toolkit bakes `.ffmap` + `.tex`; FFSmith loads and composes them. Verified **byte-identical** (100% exact pixels, max channel diff 0) to the toolkit render on a 1-layer map (g0_p0_m101) and a 2-layer / dual-slot map (g0_p0_m501).
- **M2 — field movement + camera: ✅ done.** Walk a player (arrows/WASD) around a baked map; smooth one-tile-stepped movement, facing, and a follow-camera clamped to map bounds. Verified by deterministic headless `--walk` traces (movement, bounds-block, facing).
- **M2.1 — wall collision: ✅ done.** Decoded `capk.dat` (per-tileset chip attributes); the toolkit bakes a per-cell passability grid (FFM1) and `Field::isSolid` blocks walls/objects. Verified: solids overlay exactly on walls & furniture; the player stops at interior walls.
- **M3 (core) — event VM + NPCs + dialogue: ✅ done.** The baker emits structured events (FFM2); the engine runs an event-script VM, places NPCs (solid), and face-to-talk (Z/Enter) opens a placeholder dialogue box from the script's messages. Verified headless (`--events`): NPC at (4,6) -> messages 170-175.
- **M3b (sprites) — real field sprites, facing + walk animation: ✅ done.** Player + NPCs render as actual `fldchr` sprites (feet-aligned, transparent), facing the right way and **animating while walking**. Layout from **per-sprite `field_anm` geometry** (toolkit 0.7.20 bakes `data/spritegeo.bin`: each sprite's real default frame {x,y,w,h} + part-offset anchor + an `isObject` flag from whether it has a walk-cycle). **Object sprites** (chests/doors/crystals/etc. — the 18 cycle-less entries) now draw their REAL frame at the correct anchor instead of being cropped to the 48×48 character grid (verified: img7 renders as a clean treasure chest). Characters still use the 48×48 walk template (`field_anm` fldchr1): 48×48 cells, origin (1,1), pitch 50 — **rows = facing** (Down=y1, Up=y51, Left=y101, **Right = Left flipped**), **cols = frame** (idle=x1, walkA=x51, walkB=x101). Verified in-engine via `--fieldshot --face`.
- **M3b (triggers) — step-on triggers: ✅ done.** Walking onto an invisible scripted trigger event (`img<=0`, boot 2-8 per the engine's `GetEventBootCondition` switch: 1=talk, 6=0x1b, 7=0x1c range-always, ...) auto-runs its script via the VM (e.g. g0_p0_m200 (1,1) → dialogue msg 51). Verified headless.
- **M3b (warps) — cross-map warps (script `MapChange`): ✅ done.** A `MapChange` (0x41) in an event script warps the player: the VM extracts the destination (map id + dest x/y/dir), the engine resolves the map id to a bundle map (`find_map_key`), loads & composes it, and repositions the player. The loop runs a frame at a time (`Host::frame`) so maps swap live. Verified headless: a step-trigger `MapChange`→map 888 @(3,3) loads the new 7×7 map and drops the player at (3,3); an absent target id fails cleanly.
- **M3b (doors) — door & map-edge warps: ✅ done.** Real doors/stairs warp via the script-variable idiom (`0x6B` BulkSetVars `sub2` sets var0=dest map / var2=x / var3=y / var4=dir; `0x66` SetEntityAction action `0x04` executes it) — not a header field as first assumed, nor the rare `MapChange`. The VM decodes it; the engine loads + repositions. Verified on real data: m501's exit door → town m500 @(37,28) and m500's building door → interior m501 @(6,10) (a coherent round trip); 4507/4514 warp records across the game resolve in-bounds.
- **M3b (dialogue) — real on-screen text: ✅ done.** SetMessage shows actual words in a bordered, word-wrapped, `\n`-aware dialogue box. Field dialogue is **per-area**: the engine loads `msg{N}.msd` (`ReadStoryMessageData`→`SetMessageList`→`FieldClass+0x380`), and maps select the bank by **group** (16 groups ↔ 16 banks). msg-file format: count + messages × 6 languages × 2 slots (text + speaker name); **English text = msg×12+2**. The toolkit bakes `text/msg{group}.bin` + a font atlas; the engine loads the right bank per map (reloading on warp). Verified via `--fieldshot --open-msg`: m501's NPC (msg 170) shows “Hey, Hero…”, m500's cutscene wraps across lines. *(An earlier cut used system_message.msd §4 — a coincidentally-coherent but wrong source; corrected to the real `msg{N}` banks.)*
- **M4 (state machine) — title → field → menu: ✅ done.** A top-level mode machine (mirroring `GameClass::MainFunc`: Boot/Title/Field/Battle, with Menu/Dialog as **overlays**) boots to a real **interactive main menu** over the `TitleLogo` — **New Game** (fresh party → **intro cinematics** → field), **Continue** (load `save.dat`, greyed if none), **Debug Menu** (the launcher) — and from the field opens a cursor menu (Item/Equip/Status/Save/Quit) over the **paused** field (**Enter** open, **X** close; Quit exits). `ChangeMainFunc` = current/next-mode + scene Init/Exec/End. Verified via `--title` screenshot (the 3-item menu) + `--menutest` (New Game resets party→start map, Continue→load, Debug→launcher). New Game start map = `--start-map` (placeholder `g0_p0_m500` pending the real opening). The original's New Game runs `InitGameData`+`ReadStartData`+`e3_param.dat` (start party); Continue = `FieldMapContinue`. **New Game intro cinematics** play before the field: the **prologue text crawl** (the real 'In an age long past… Avalonian Empire…' string, extracted by content from `msg0.msd` → `data/intro.bin`), the **logo**, then the **'Prologue'** chapter card (advance with Z). Verified via `--intro 0|1|2` screenshots + `--menutest` (New Game → Intro → 3 beats → start map).
- **M5 (menu pages) — Item / Equip / Status: ✅ (core).** The field menu's pages show **real decoded data**: a scrollable 500-item list with descriptions (**Item**), and each of the 21 roster characters with their actual equipment resolved to item names (**Equip**) and stat descriptions (**Status**) — e.g. Sol with Orichalcum (ATK 23). Baked from `system_message.msd` + `boot_data` + `chara_set.dat`; navigated with the cursor (Item: ↑↓ + L/R ±10; Equip/Status: ←→ cycle character; X back). Verified by screenshot (`--menupage 1|2|3`). Save is a stub (needs save state); Quit exits.
- **M6 (battle) — turn-based scaffold: ✅ (step 1).** A real battle mode: a battle scene (real **battle background**) with a **real enemy from the monster table** (name + HP/attack/defense, e.g. Goblin 21/10/3), the **named party** (Sol/Aigis/Dusk/Sarah from `chara_set`) with HP bars, and a turn loop — **Attack / Defend / Run**, damage calc, enemy turns, win/lose. **M6.2:** battles are now party-vs-**group** (1-3 real enemies, difficulty-banded around the lead so a Goblin pack never includes a Mud Golem; duplicate names auto-suffix "Goblin A/B"), with **target selection** on Attack and per-enemy turns. Start with the field hotkey **B** (random encounter) or `--battle N`; verified headless with `--battlesim N` (Goblin → Victory in 5 steps). **Battle loop closed:** real decoded stats (STR/SPD/VIT/INT/MND + job/level), ATB by SPD, Magic, the exact `CalcPhysicAttackDmg` formula, and now **EXP/gil rewards + level-ups + revive** — on victory each survivor gains the monsters' **EXP** (`monster body[6]` BE u32) and the party gains **gil** (`body[10]`); crossing an EXP threshold (`levels.bin`, from section-8 `[0x10+9i]`) **levels the member up** and recomputes max HP/MP from the section-8 growth curve (verified Sol L3→L5, HP 70→96). KO'd members are revived by **Phoenix Down** (item 426, "Cures Knocked Out" → ~25% HP). All persists into the party + `save.dat` (`FSAV` v4: + per-member level/exp). The five battle attributes (STR/SPD/VIT/INT/MND) are **job-derived** -- `memberStat()` = section-8 per-level base-stat x the job's stat% (`SetJobStatus`, libjniproxy 152644-152705, FF5-PC `FUN_00468490` cross-check) -- so attack/defense/magic scale with job + level (raw-STR placeholder retired). Verified `--battlesim` (Win! +22 EXP +11 G), `--leveltest`, `--revivetest`, `--jobstattest` (Sol L3 -> HP60 MP19 STR10 SPD10 VIT9 INT6 MND6).
- **Character stats decoded: ✅.** `chara_set.dat` per-character **STR / SPD / VIT / INT / MND** (+ job, level) are decoded (engine-verified via `ReadStartData`/`MEMBER_STATUS`) and shown on the **Status** page (e.g. Sol Lv3 STR20 SPD5); battle damage now uses **real STR** (Sol hits for ~17, Aigis ~8). HP/MP are now **real** too (decoded growth table, boot_data §8 — Sol L3 HP70/MP23, shown on Status + used as battle max-HP), and the battle runs an **ATB turn order by SPD** (the gauge fills proportional to SPD, so Sarah SPD14 acts ~2.5x as often as Sol SPD5). Battles also have a **Magic** command (real spell table — Cure/Fire/Blizzard/…; each member knows spells by INT/MND, gated by MP; damage scales with INT, heals with MND). Damage uses the **decoded `CalcPhysicAttackDmg` core** wired to real BTLACT inputs (see `docs/BTLACT_MAP.md`): `(weaponATK<<6 + spread − defense<<6) × (3·STR + level) / 1024`, with weapon ATK / armor DEF parsed from the item table — so it scales with **equipment + STR + level** (Aigis w/ Graham's Sword lv10 → ~110, Dusk w/ Short Sword → ~11, unarmed Sarah → 1). Magic scales with INT vs a magic-defense proxy. Remaining (full exact-match): the job-derived attack stat for `A`, the crit / element / race / status / hit-count modifiers, status effects, per-job HP%/MP%, encounter tables + rewards.
- **M7 (save/load) — field-state persistence: ✅.** The game saves to `save.bin` (15 KB/slot via `GameClass::SaveGameData`/`SetGameData` — full party MEMBER_STATUS, inventory, gil, position, flags). FFSmith persists its current mutable state — **current map + player tile + facing + sprite** — to `save.dat` (`FSAV` v2): **Save** in the field menu or **F5**; **F9** or `--load` (Continue) restores it on boot; `--save` writes a one-shot. Verified round-trip (save m501@(6,11) → reload resumes there). **Now also persists the party (current HP/MP), inventory, and gil** (see below).
- **Persistent party / inventory — ✅.** A mutable `GameState` (the 4-member party with **current HP/MP**, an **inventory** of item+count, and **gil**) is initialised from the baked `chars.bin` and now survives across battles and saves. Battle builds combatants from it and **writes HP/MP back** when the fight ends (verified: Aigis leaves a fight at 198/199, not refilled). The **Item** menu lists the **owned inventory** (counts + gil) and **using a Potion** heals the most-wounded member and decrements the stack (`[A] Use`; verified Sol 1→70 HP, Potion ×5→×4). **Equip is actionable too**: each member keeps a mutable `equip[6]` (seeded from `chara_set`, incl. Aigis's dual-wield Graham's Sword + Masamune); on the Equip page pick a slot → `[A]` shows the fitting inventory gear matched by **baked `item_type` category** (weapons in hand slots, shield in off-hand, hat→head, body→body, def-less accessories→arms/acc.) + `[Remove]` → swapping recomputes battle ATK/DEF and returns the old piece to the bag (verified Orichalcum A23 → Broadsword A12; Leather Shield→off-hand, Power Armlet accessory equippable). Status shows live HP/MP. Party (HP/MP **+ equipment**), inventory, and gil are serialized into `save.dat` (`FSAV` v3) and restored on load (round-trip verified byte-exact, 225 B). Headless self-tests: `--itemtest`, `--equiptest`.
- **Next:** EXP/gil battle rewards + level-ups, then encounter tables.

Full plan and the reverse-engineering map: **`ENGINE_RE_ROADMAP.md`**.

## Build

Requires a C++17 compiler, CMake ≥ 3.16, and SDL2 (dev headers + lib).

```sh
cmake -S . -B build
cmake --build build
./build/ffsmith
```

On Windows: install SDL2 (e.g. `vcpkg install sdl2`, or the SDL2 development package) and configure CMake with the vcpkg toolchain or `-DSDL2_DIR=...` as usual.

- **Field polish — animated tiles + smooth camera: ✅.** Water/torch **tile animation** is driven by the real chip attributes (`FieldClass::GetUpdateChipID`): in `capk.dat`'s 7-byte chip record (word A) bit 8 = animated, bits 9-10 = type (loop / ping-pong), bits 11-14 = frames, bits 15-17 = speed. The toolkit (0.7.14) bakes `data/chipanim.bin` (per-tileset animated chips; 1136, all 3-frame); the engine cycles `base..base+frames-1` and **overdraws the current frame** of each animated cell (under sprites) each tick. Verified: the overworld's 22,307 water cells ripple across 3 frames (`--animtick 0|8|16`, 281k px change/frame). The **camera** already follows the player's interpolated pixel position (`pixelX/pixelY`+`prog_`) with edge clamping = pixel-smooth scroll. **Damage floors** are wired too: the chip floor-attribute `(A>>18)&0x3f` (bit `0x10` = damage, per `IsDamagedFloor`) is baked to `data/chipfloor.bin` (toolkit 0.7.15); the engine flags **walkable** damage cells and drains ~1/16 max-HP from each party member per step (persists in the party state). Verified `--dmgtest`: stepping onto lava on g12_p0_m1 took Sol 70->66, Aigis 199->187. *(The damage decode is concentrated in one dungeon tileset (mc63, 47 chips) and matches the cracked-spike hazard terrain on-screen.)* **Sprite z-ordering** is in: the original depth-sorts chips+chars and splits ground vs overhead at a per-map threshold (`FieldClass+0xdc2c`, read from the map header, **default 0**). Verified that layer 0 = ground (+walls/collision) and **layers 1+ = overhead** (tree canopies, roof edges, awnings — all on transparent bg). The engine now composes ground and overhead separately (`compose_range`) and draws overhead **after** sprites, so the player walks behind canopies/roofs (`--no-overhead` toggles it; verified OFF vs ON on g0_p0_m500). The split point is the **baked per-map threshold** (`FieldClass+0xdc2c`, toolkit 0.7.16 -> FFM2 reserved u32): layers with index > threshold are overhead. Verified the engine applies it per map: m500 splits at L0 (layer 1 overhead), the g5_p1_m500 cave at L1 (both layers ground), m2601 at L1 (only layer 2 overhead). Distribution: 1432 maps at 0, 220 at 1, 3 at 2.

## Run / controls

```sh
./build/ffsmith                 # windowed, runs until you quit
./build/ffsmith --frames 120    # headless/CI: run 120 ticks then exit
./build/ffsmith --scale 4 --hz 60
./build/ffsmith --help
```

Arrows / WASD = move · Z confirm · X cancel · Enter/Tab menu · F1-F4 debug · F5 save / F9 load · Esc quit. In the **Item** menu, **Z = Use** (Potion heals the most-wounded member).

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

Headless options (no window): `--shot out.tex` writes the composed map; `--walk URDL...` prints the player's tile path for a scripted move sequence; `--fieldshot out.tex` captures a rendered field frame (add `--frames N` to let NPC wander / tile animation run N ticks first); `--npctest` runs the NPC auto-wander self-test on a synthetic map (no bundle needed).

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

**Party + menu (0.1.2):** FFD dual Light/Dark parties (5 each, `switchSide`, FSAV v6), the full FFD main-menu command set (Item/Magic/Equip/Job/Status/Formation/Config/Save) with the FFD window skin, a Formation tab (reorder/add/remove/switch side) and a Debug **Party** toggle.
