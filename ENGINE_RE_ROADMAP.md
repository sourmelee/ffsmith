# FFD Engine Reverse-Engineering Roadmap

*Created 2026-06-01. Working folder for all engine work: `Engine/`.*

## Locked decisions (from kickoff)

| Decision | Choice |
|---|---|
| **End goal** | A **runnable reimplementation** — a new engine that loads the original data/assets and actually plays the game. |
| **Reference** | **Android native C (`libjniproxy.so`) is ground truth**; readable **Mobile Java** is the cross-reading guide for shared logic. |
| **Target language** | **C/C++** — closest 1:1 mapping from the decompiled native code. |
| **First subsystem** | **Field movement / collision / camera.** |
| **Engine name** | **FFSmith** (working/test name — a nod to the `Mtx` = "Matrix" framework). |
| **Asset pipeline** | Toolkit **bakes** verified exports that FFSmith consumes directly (`docs/ASSET_PIPELINE.md`). Toolkit ⇄ Engine are symbiotic. |

Everything below is organized around those four choices.

---

## Part 1 — The Map: what we already know

The project to date is overwhelmingly an **asset/data** effort. That work is ~90% done and is *directly reusable* as the new engine's content pipeline. The **runtime engine** is the frontier: only map-loading and the event-script VM are decoded; battle, field movement, menus, state machine, and save are open.

### 1.1 Asset & data layer — SOLVED, reuse as-is

These formats are fully decoded and implemented in the `Python/ffd/` toolkit. The new engine consumes the *same* files; we port the parsers (or call the toolkit as an offline asset baker).

| Domain | Status | Toolkit reference |
|---|---|---|
| OBB container (XOR-0x14, FAT) | ✅ decode + byte-identical repack | `ffd/containers/obb.py` |
| ICP / INP images (paletted PNG) | ✅ pixel-perfect (2137/2137) | `ffd/android_export/icp.py` |
| mtxs audio (OGG) | ✅ valid playback | `ffd/music/` |
| Mobile `.sp`, IC images | ✅ parse + render | `ffd/containers/sp.py`, `ffd/images/ic.py` |
| Android map packs (mpkh/mpk) | ✅ index + chunk parse | `ffd/maps/` |
| **Map structure + mc_id (tileset)** | ✅ engine-accurate (`LoadMapInfo` decode) | `parse_android_map_engine` |
| `boot_data.dat` (16 sections) | ✅ all enumerated; magic/items/jobs/abilities/monsters parsed | `ffd/boot/`, `ffd/items|jobs|monsters|abilities/` |
| Animations (field/battle) | ✅ `field_anm`, `btlanm_sp` | `ffd/animation/` |
| Text / MSD | ✅ multi-language | `ffd/text/` |

**Implication:** the engine never has to re-solve "where do the bytes live / how is a tile drawn." That groundwork exists. The unknowns are all *behavioral* — what the engine *does* with those bytes per frame.

### 1.2 Engine runtime — what's decoded vs. open

| Subsystem | State | Where |
|---|---|---|
| Map load → tile/layer/mc_id resolution | ✅ decoded | `FieldClass::LoadMapInfo` (c:106317) → `parse_android_map_engine` |
| Event-script VM (opcodes 0x00–0xab, exec model, NPC/trigger records) | ✅ decoded | `FieldClass::MoveScript` (c:125490); export §16 |
| **Field movement / collision / camera** | ❌ **open — first target** | `FieldClass` (see Part 4) |
| Game state machine / scene dispatch | ❌ open | `GameClass::ChangeMainFunc` (c:144756) |
| Save / load format | ❌ open | `GameClass` save funcs |
| Menu system | ❌ open | `MenuClass` (278 methods) |
| **Battle engine** | ❌ open — largest gap | `BattleClass` (636 methods) |

### 1.3 Engine architecture — the target shape

The Android binary is **254K lines of Ghidra C**, but it is cleanly class-structured. Method counts from `libjniproxy.so_new.h`:

```
FieldClass   1159   field/map engine: movement, NPCs, camera, events, rendering
GameClass     722   top-level state machine, scenes, boot, save/load
BattleClass   636   entire battle system (turn schedule, damage, status, rewards)
MenuClass     278   menus (+ JobsMenu* widget family)
Mtx*          ~600  reusable Square-Enix middleware ("Matrix" framework)
SoundCtrlClass / PaletteCtrlClass / SpriteClass   subsystem controllers
PurchaseScene / MtxPurchaseManager   IAP — IGNORE (not gameplay)
```

Two structural facts drive the whole reimplementation plan:

**(a) Framework vs. game logic split.** `Mtx*` classes are generic engine middleware (graphics, texture/sprite managers, font, particle, sound, file I/O, touch, animation `MtxAnmCtrl`, effects `MtxEfcCtrl`, `MtxBinaryReader`). The four `*Class` types are the *game*. We **reimplement `Mtx*` as a thin host layer** on a modern stack (SDL2 + OpenGL/your choice) and **port the four game classes faithfully**. We do not reverse the middleware byte-for-byte — only its observable contract.

**(b) FFV is a Rosetta Stone.** `libff5lib.so` (Final Fantasy V, decompiled alongside in `Decomp/FFV_FFD_compare/`) is built on the *same* `Mtx*` framework. The earlier gamepad port was lifted from FFV precisely because the shared engine matches. When an FFD function is opaque, diff it against its FFV twin — naming and structure are often clearer there.

**Mobile ↔ Android correspondence** (use Mobile Java to read intent, Android C for ground truth):

| Role | Mobile (Java) | Android (C) |
|---|---|---|
| App entry / MIDlet | `class_19` (`FFL extends IApplication`) | JNI `MainActivity_*` |
| Main loop / canvas / input | `class_20 extends Canvas` (game loop, `Random`, net) | `MainActivity_render` (c:206254), `_touch` (c:206219) |
| Field / map / event engine | `class_10` (`method_482` dispatcher, `method_264` NPC loader) | `FieldClass` |
| Stat / battle tables & math | `class_16` (14k lines, growth tables), `class_5` | `BattleClass`, `GameClass` |
| Low-level graphics / palette | `class_2` (Object3D/Texture), `class_18` | `MtxGraphics3D`, `MtxTexture`, `SpriteClass` |
| Audio | `class_1` (`MediaListener`) | `SoundCtrlClass`, `MtxSound*` |

---

## Part 2 — Reimplementation strategy (C/C++)

### 2.1 Architecture of the new engine

```
ffd-engine/
  src/
    host/        # SDL2 window, GL context, input, audio, timing  (replaces Mtx* platform glue)
    mtx/         # reimplemented Mtx framework CONTRACTS only:
                 #   texture/sprite manager, font, anim (MtxAnmCtrl), effects, binary reader
    data/        # asset loaders: OBB, boot_data, maps, IC/ICP, anm  (port of ffd/ parsers)
    field/       # FieldClass port  <-- first real gameplay code
    game/        # GameClass: state machine, scene dispatch, save
    battle/      # BattleClass (later)
    menu/        # MenuClass  (later)
  assets/        # baked content (or load main.obb directly at runtime)
  docs/          # per-subsystem behavioral specs (one .md per subsystem)
  tests/         # golden-data tests vs. toolkit output
  third_party/   # SDL2, glad, stb, etc.
  CMakeLists.txt
```

### 2.2 Data-driven from day one

The engine should read the **original `main.obb`** (or a baked dump from the toolkit) so content is never a variable while we debug behavior. The toolkit already produces verified-correct tiles, maps, palettes, animations — treat its output as **golden test data**. Any C parser we write must match the Python parser's bytes/pixels exactly before we trust it.

### 2.3 The per-subsystem RE loop

Repeat this five-step loop for every subsystem, smallest-first:

1. **Locate** — list the class's methods from the `.h`; find the entry point and its line in the `.c`.
2. **Cross-read** — open the Mobile Java twin to recover intent and variable meaning (Ghidra loses names; Java keeps structure).
3. **Spec** — write `docs/<subsystem>.md`: state struct + offsets, control flow, formulas, edge cases. Plain-language behavior, not a C transcription.
4. **Implement** — port to C/C++ against the spec, reusing the host/mtx shims.
5. **Verify** — run it against the original and a golden trace; fix divergence; only then move on.

Specs are durable even if a given implementation pass stalls — write them down.

---

## Part 3 — Pre-RE setup steps (do these *before* touching gameplay logic)

These are the steps to take **before** attempting the engine itself — they de-risk everything after.

### Step 0 — Scaffold `Engine/`
Create the skeleton in 2.1. Initialize git (or a subfolder of the existing repo). Add this roadmap and an empty `docs/`. Decide: **C or C++?** Recommendation: **C++17** — the decompiled code is C-with-classes; C++ lets each `*Class` map to a real class and keeps `this`-pointer logic readable. Drop into plain C only for hot loops if needed.

### Step 1 — Toolchain + empty loop
Stand up CMake + SDL2 + a GL loader (glad). Goal: a window that opens, clears, and runs a fixed-timestep loop calling `host_poll_input()` / `update(dt)` / `render()`. This is the skeleton that `MainActivity_render` maps onto. **No game code yet** — just prove the loop, input, and a textured quad draw.

### Step 2 — Asset bridge + static map render (the first real proof)
Port the *minimum* asset path: OBB loader → one `mpk` map chunk → `parse_android_map_engine` (tile/layer/mc_id) → load `mc{N}_{V}.png` tilesheet → draw the map statically to the window. **No movement, no collision** — just render a known map and eyeball it against the toolkit's MapTab render of the same map. When they match pixel-for-pixel, the entire content pipeline is trustworthy and the field engine has solid ground to stand on.

> Verification harness: dump the toolkit's render of map `gG_pP_mM` to PNG; screenshot the engine's render of the same map; diff. This same harness is reused for every later visual subsystem.

### Step 3 — Ghidra navigation kit
Working a 254K-line file needs tooling, not scrolling:
- **The `.h` is the map.** `grep -oE 'FieldClass::[A-Za-z0-9_]+' …h | sort -u` gives the full method surface of any class.
- Build a **function index**: name → line in `.c` (a one-time grep to a lookup file).
- **FFV diff**: keep `libff5lib.so.c/.h` open; when an FFD function is opaque, find the same-named FFV function and read it side-by-side.
- Re-use the **already-recovered offsets** as anchors (Step 4).

### Step 4 — Recover the `FieldClass` state struct
Movement code is meaningless without the struct it mutates. We already have confirmed offsets — seed the struct and grow it as functions are read:

| Offset | Meaning | Source |
|---|---|---|
| `this+0xdc18` | map width (cells) | `LoadMapInfo`, confirmed again in `CheckMovePass` |
| `this+0xdc1c` | map height (cells) | same |
| `this+0xdc38` | layer/entity count bound | `CheckMovePass` loop bound |
| `this+0xdc40` | entity/chara record table, **0x28-byte stride** | `CheckMovePass` (`+3`,`+4` = X/Y wrap flags) |
| `this+0x5b2 / 0x5b4` | map header bools/shorts | `LoadMapInfo` |
| `this+0xe9ac` | script variable array | `MoveScript` |

Write these into `docs/field_state.md` and `src/field/field_state.h` first.

---

## Part 4 — First subsystem: Field movement / collision / camera

This is the first gameplay system. It is self-contained, **visually verifiable within a day of work**, and exercises input → logic → render end-to-end. Below is the actual call graph from `libjniproxy.so_new.c`.

### 4.1 Per-character state machine (the core)
Every actor (player + NPCs) is ticked through one of these states each frame. Port `MoveChara` as the dispatcher:

```
MoveChara  (dispatch on chara state)
 ├─ MoveCharaControl   player, reads input
 ├─ MoveCharaAuto      NPC autonomous walk
 ├─ MoveCharaEvent     script-driven movement
 ├─ MoveCharaWait      idle
 └─ MoveCharaHide      inactive
        ↓ (when a step is committed)
 MoveCharaAction → MoveCharaActionOfWalk / ...OfJump / ...OfFly   (+ Before/After hooks)
 InitCharaState{Control,Auto,Event,Wait,Hide}   set state + entry params
```

### 4.2 The input → move → collision → scroll pipeline

| Stage | Function(s) | Line |
|---|---|---|
| Read input | `FieldClass::GetInputKeys`, `GetReferenceInput` | — |
| Keys → direction | `KeyStateToMoveFlag`, `CommandToMoveDir`, `WalkCommandToDirCommand` | `CommandToMoveDir` c:117569 |
| Direction → pass-test | `DirToMoveFlag`, `DirToPassFlag`, `PassFlagsToCharaCommand` | — |
| **Collision check** | **`CheckMovePass`** | **c:114793** |
| Tile passability | `GetChipAttributeInfo{,OfChipID,OfPosition}`, `GetPassFlags{,OfAuto,OfControl}` | — |
| Resolve target cell | `GetAfterPositionOfWalk`, `ToMoveCellX/Y`, `ToMoveX/Y`, `ToMoveCellXOfRound` | `GetAfterPositionOfWalk` c:118468 |
| Commit player move | `MovePlayer`, `IsMovePlayerActive`, `IsPlayerMoveEnd` | `MovePlayer` c:113088 |
| Camera follow | `MoveScroll`, `InitScroll`, `GetCurrentScrollX/Y`, `GetScrollDistance*`, `ScrollToOffsetCellX/Y`, `IsAddScroll` | `InitScroll` c:130788 |
| Player init | `InitPlayer` | c:108012 / 114527 |
| Frame draw | `DrawMainProcess`, `DrawMapChips`, `MoveMainProcess`, `ModeMain` | — |

**`CheckMovePass` already partially read:** returns pass/block; first gates on a flag mask `(*(param_1+0x48) & 0xc40)`; normalizes target X/Y with modulo against width (`+0xdc18`) / height (`+0xdc1c`) **only when the per-map wrap flags** (`record+3`, `record+4`) are set — i.e. world maps wrap, dungeons clamp. This is the kind of detail the spec must capture.

### 4.3 Mobile cross-read
`class_10` holds the Mobile twins. The movement dispatch and the `field_312` state values (`0x20000`=field, `0x30000`=event, `0x100000`=battle) from the event-VM work (export §16.6) tell you how field hands off to event/battle — reuse that.

### 4.4 Verification plan
1. Load a known, *non-wrapping* indoor map (simplest collision).
2. Spawn the player via `InitPlayer` defaults; render with the Step 2 pipeline.
3. Drive D-pad input; confirm: walks on floor, blocks on walls, can't leave bounds.
4. Add a *wrapping* world map; confirm edge wrap matches `CheckMovePass` modulo logic.
5. Confirm camera centers on player and clamps/wraps with the map.
6. **Golden trace:** instrument positions per frame for a fixed input script; compare against the original running in an emulator with the same inputs.

### 4.5 Definition of done ("field is walkable")
Player walks a real map with correct tile collision, map-edge behavior (wrap vs. clamp), and a following camera, rendering the actual OBB tilesheets — matching the original for a scripted input sequence. NPCs, triggers, events, and battles are explicitly **out of scope** for this milestone (they come next, and the event-VM is already decoded to receive them).

---

## Part 5 — Milestones

| # | Milestone | Gate |
|---|---|---|
| M0 | Empty SDL2 loop + textured quad | window runs, draws |
| M1 | **✅ Static map render** from baked bundle | byte-identical to toolkit (g0_p0_m101, m501) |
| M2 | **✅ Field walkable** (Part 4) | walk + follow-camera + bounds + `capk.dat` wall collision (verified g0_p0_m501) |
| M3 | **VM + NPCs + dialogue + sprites + step-on triggers + warps + real dialogue text** ✅ (door/edge warps via script-var idiom; field text from `system_message.msd`); **M4 = next** | talk + warp round-trip + on-screen text (msg 170/150) verified |
| M4 | **✅ Game state machine** (mode dispatch mirroring `GameClass::MainFunc`: Boot/Title/Field/Battle; Menu/Dialog = overlays) | title (real `TitleLogo`) → field (Start/Z) → menu overlay (Enter/X) verified by screenshot |
| M5 | **◑ Menu pages** — Item/Equip/Status show real baked data (items + roster + equipment); use/equip **actions** need party+save (M5 cont.) | screenshots: 500-item list, Sol's Orichalcum, ATK 23 |
| M6 | Battle engine (`BattleClass`) | turn loop + damage match original |
| M7 | Save/load | round-trip a save |

M2 is the immediate objective; everything in Part 3 precedes it.

---

## Part 6 — Risks & open questions

- **Ghidra fidelity.** Decompiled ARM64 mangles types (`undefined8`, SIMD temporaries in `render`). Trust the *structure*, verify *behavior* against runtime traces — don't transliterate.
- **`Mtx*` contract scope.** We must reverse enough of each `Mtx*` class to reproduce its *observable* behavior (e.g. how `MtxAnmCtrl` advances frames), not its internals. Risk of scope creep here — timebox it.
- **Timing model.** Need to confirm the original's frame/tick rate and whether logic is frame-locked (likely) vs. time-based, so movement speed matches.
- **RNG.** Battle and encounters need the original PRNG (Mobile uses `java.util.Random`; Android has its own). Identify and reproduce it before M6.
- **Float vs. fixed point.** Scroll/camera use floats in the C; confirm field logic is integer-cell based (it appears to be) to avoid drift vs. the original.
- **Save format & legal.** Save layout is undecoded. Separately: this is a clean-room *reimplementation* — ship no original assets; the engine loads the user's own `main.obb`.

---

*Status 2026-06-01: M0 ✅ + M1 ✅ — FFSmith loads toolkit-baked `.ffmap`/`.tex` and composes maps **byte-identical** to the toolkit (verified g0_p0_m101, g0_p0_m501). Toolkit baker: `python ffd_toolkit.py --bake-ffsmith`. M2 ✅ incl. **`capk.dat` wall collision** (chip-attribute file decoded; FFM1 bakes a per-cell pass grid). M3 core ✅ — event-script VM + NPCs (solid) + face-to-talk placeholder dialogue (FFM2 event baking). M3b ✅ — real field sprites (facing + walk anim, `field_anm` template), step-on triggers (boot-condition switch), and cross-map warps (script `MapChange` 0x41: VM extracts map+x/y/dir, engine `find_map_key`→load→reposition, frame-stepped loop). **Door/edge warps decoded** (not header-encoded as first assumed): `0x6B` BulkSetVars sub2 sets script vars (var0=map,2=x,3=y,4=dir), `0x66` SetEntityAction action `0x04` executes — verified m500↔m501 round trip, 4507/4514 records in-bounds. **Real dialogue text done**: field messages decoded from `system_message.msd` (6-lang interleaved, EN=slot1, msg_id-indexed per `GetMessageData`); toolkit bakes `text/messages.bin` + a DejaVuSansMono font atlas; engine renders a word-wrapped dialogue box (verified msg 170 “Hero: Barbara!”, msg 150 multi-line). Per-area dialogue banks corrected to `msg{N}.msd` (was wrongly system_message §4); bank = map group, baked as `text/msg{group}.bin`, engine selects per map. Next: M4 game state machine (also resolves story-state bank variants).*
