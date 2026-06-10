# FFSmith — Engine Overview

*Audit snapshot 2026-06-10, against commit `d2aacc2` ("Intro and event logic updated"). All claims verified directly against `src/` unless marked otherwise.*

FFSmith is a clean-room C++17 + SDL2 reimplementation of the Final Fantasy Dimensions / Final Fantasy Legends runtime. It consumes **baked bundles** produced by the Python toolkit (`../Python`, `--bake-ffsmith`) and never parses raw on-disk game formats. Behavioral ground truth is the Ghidra decompilation of the Android native library (`Decomp/FFV_FFD_compare/libjniproxy.so_new.c`, ~254K lines); the Mobile Java decompilation is the cross-reading guide.

## Source layout (actual, not aspirational)

| Path | LOC | Contents |
|---|---|---|
| `src/host/host.{h,cpp}` | ~1,790 | `Host` class: SDL window/renderer, fixed-timestep loop, input edge detection, mode state machine, **all menus**, **the entire battle system**, persistent game state (party/inventory/gil), debug launcher, audio polling, text rendering, self-tests |
| `src/main.cpp` | 565 | CLI parsing, save-file I/O (`FSAV`), map load/warp orchestration loop, VM self-test, headless modes (`--walk`, `--events`, `--shot`) |
| `src/field/field.{h,cpp}` | 345 | `Field`: per-map player movement, collision, NPC/trigger interaction, dialogue/choice flow, auto-event queue |
| `src/field/event_vm.{h,cpp}` | 250 | Event-script bytecode interpreter (`run_event`) |
| `src/field/script_state.{h,cpp}` | 299 | Script flag/variable banks, `GetReference`, `CheckCondition`, `CheckEventAppear`, save-blob serialization |
| `src/data/bundle.{h,cpp}` | 449 | Loaders for every baked format (FTEX, FFM0–FFM4, FITM, FCHR, FMON, FSPL, FLVL, FSGE, FCAN, FCFL, FMSG, FMET, FSTR, FAUD) |
| `src/render/compositor.{h,cpp}` | 147 | CPU map composer — mirrors the toolkit's `ExtractTab._render_android_map`, byte-identical (PIL `div255` rounding reproduced) |
| `src/audio/audio.{h,cpp}` | 244 | `AudioManager`: single streaming BGM + pooled one-shot SFX through one SDL audio callback |

**HIGH:** Total engine ≈ 3,650 LOC. The `ENGINE_RE_ROADMAP.md` §2.1 target layout (`src/mtx/`, `src/game/`, `src/battle/`, `src/menu/`, `tests/`, `third_party/`) was **not** followed — game-state machine, battle, and menus all live inside `Host`. See `../development/refactoring_candidates.md`.

## Original-engine class mapping

| Original (Android C) | FFSmith | Fidelity |
|---|---|---|
| `MainActivity_render` loop + `Mtx*` platform glue | `Host::frame/run` + SDL2 | Contract-level reimplementation, intentional |
| `GameClass` (state machine, scenes, save) | `Host::Mode` enum + `main.cpp` save I/O | Mode dispatch mirrors `ChangeMainFunc` shape; save format is FFSmith's own (`FSAV`), **not** the original `save.bin` |
| `FieldClass` (1159 methods) | `Field` + `EventVM` + `ScriptState` + parts of `Host` | Movement/collision/events ported behaviorally; camera simplified; NPC `MoveCharaAuto` walk **not implemented** (NPCs stand still) |
| `BattleClass` (636 methods) | `Host::updateBattle` etc. (~400 LOC) | Core damage formula decoded + ported; everything else approximated (see `../systems/ui.md` and `ffsmith_status.md`) |
| `MenuClass` (278 methods) | `Host::updateMenu/renderMenu` etc. | Functional equivalent, not a port |
| `SoundCtrlClass` | `AudioManager` | Contract-level (ReserveBGM/ReserveSE semantics) |

## Data ownership model

- **`Host` owns persistent state**: `gameParty_` (vector of `GameMember`: charIdx, current HP/MP, `equip[6]`, level, exp), `inventory_` (`InvSlot` id+count), `gil_`, `scriptState_` (event-VM flags/vars), all static data tables loaded from the bundle (`items_`, `chars_`, `monsters_`, `spells_`, `levels_`, `spriteGeo_`, chip tables), and the common-event pool.
- **`main.cpp` owns the current map** (`FfMap m` plus composed `Texture` framebuffers) and the `Field` instance (a `unique_ptr` recreated on every warp/load). `Field` holds a raw `const FfMap*`; `Host` holds a raw `Field*`. Map swaps are safe only because `loadInto` reassigns and re-wires in one place. **MEDIUM risk:** this raw-pointer triangle is fragile; see technical_debt.md.
- **Battle uses copies**: `startBattle` builds `Combatant` vectors from `gameParty_` + equipment; `endBattle` writes HP/MP back. Rewards/level-ups mutate `gameParty_` directly in `awardBattleRewards`.
- **`ScriptState` is shared across maps** (owned by Host, injected into each new `Field` via `setScript`), serialized into the save (`SST` blob, ~4,032 bytes).

## External dependencies

SDL2 only (CMake config package, pkg-config fallback). No GL loader, no stb, no third_party/ — the roadmap's "SDL2 + glad" plan was simplified to pure `SDL_Renderer`. Audio needs no decoder: bundles carry IMA-ADPCM WAV that `SDL_LoadWAV` handles natively (ffmpeg is a *bake-time* dependency in the toolkit, not an engine one).

## Verification approach

No checked-in unit tests. Verification is via headless self-test flags compiled into the binary, run with `SDL_VIDEODRIVER=dummy`:
`--vmtest` (11 VM checks, no SDL), `--battlesim N`, `--itemtest`, `--equiptest`, `--leveltest`, `--revivetest`, `--menutest`, `--dmgtest`, `--walk URDL.C`, `--events`, `--shot`/`--fieldshot` (pixel dumps diffed against toolkit renders). The M1 gate was byte-identical map composition vs the toolkit (verified g0_p0_m101, g0_p0_m501). GUI feel and audio are verified by Jack on Windows.

## Related documents

- `ffsmith_status.md` — per-subsystem implementation status with confidence ratings
- `runtime_pipeline.md` — the frame loop and mode machine in detail
- `repository_relationships.md` — how Engine, Python toolkit, and Decomp interrelate
- `../systems/*.md` — per-subsystem specs
- `../ASSET_PIPELINE.md` (pre-existing) — toolkit⇄engine contract; **note its "Bundle layout (v0 draft)" section is stale** (lists `data/*.json`; the real bundle uses binary `data/*.bin` — authoritative spec: `../../Python/docs/architecture/asset_pipeline.md`)
