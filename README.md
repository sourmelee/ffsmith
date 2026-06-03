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
- **Next:** a font/text pass for real dialogue; range-trigger extents (multi-tile boot-6/7 zones), common-event (boot 4/5) and auto/parallel (boot 0) events.

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

Arrows / WASD = move · Z / Space = confirm · X = cancel · Enter = menu · Esc = quit.

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
