# FFSmith

Clean-room reimplementation of the **Final Fantasy Dimensions / Final Fantasy Legends** engine, in C++17. The working name nods to the engine's own `Mtx` ("Matrix") middleware.

Companion to the Python **FFD/FFL Toolkit** (`../Python/`): the toolkit bakes verified assets that FFSmith loads, and the two are developed together. The toolkit's parse output is FFSmith's source of truth and its golden test data. See `docs/ASSET_PIPELINE.md`.

## Status

- **M0 — host + main loop: ✅ done.** SDL2 window, fixed-timestep logic loop, input edge-detection, headless `--frames` mode. Verified compiling clean (`-Wall -Wextra -Wpedantic`) and running headless (120 ticks, clean exit).
- **M1 — static map render: ✅ done.** Toolkit bakes `.ffmap` + `.tex`; FFSmith loads and composes them. Verified **byte-identical** (100% exact pixels, max channel diff 0) to the toolkit render on a 1-layer map (g0_p0_m101) and a 2-layer / dual-slot map (g0_p0_m501).
- **Next:** M2 — field movement / collision / camera.

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
