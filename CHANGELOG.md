# Changelog

All notable changes to **FFSmith** (the clean-room FFD/FFL engine) are recorded
here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html):

- **MAJOR** — a stable line, or a breaking change to a persisted contract:
  the `FSAV` save format or the baked-bundle format in a way that makes
  existing saves/bundles incompatible.
- **MINOR** — a new milestone or subsystem (a game mode, a menu page, a
  battle feature, audio, a new self-test / CLI mode) — backward compatible.
- **PATCH** — bug fixes and small internal changes.

The canonical version string lives in [`src/version.h`](src/version.h)
(`FFSMITH_VERSION`); `CMakeLists.txt` `project(... VERSION ...)` mirrors it.
Bump it in the same commit as the changelog entry, and promote `[Unreleased]`
to a new versioned section on release. Per the project conventions
([`../CLAUDE.md`](../CLAUDE.md)) PATCH bumps happen freely; a MINOR or MAJOR
bump is confirmed first.

The authoritative "where is the engine actually at" document — including the
full list of approximated/heuristic subsystems — is
[`docs/architecture/ffsmith_status.md`](docs/architecture/ffsmith_status.md),
not this log.

## [Unreleased]

## [0.1.1] - 2026-06-14

### Added

- **N2 job-derived battle stats** (`SetJobStatus`).  `Host::memberStat(i, which)`
  derives each of the five attributes as `max(1, base_stat[level] * jobPct / 100)`
  from the FLVL per-level base-stat byte and the FJOB per-job stat% (libjniproxy
  152644-152705; FF5-PC `FUN_00468490` cross-check).  The battle combatant build
  now uses it for attack `A` (STR), defense (VIT), and magic (INT/MND), retiring
  the raw-STR `A` placeholder.
- `--jobstattest` self-test: recomputes the derivation independently and asserts
  `memberStat()` agrees + prints the derived party stats (PASS/FAIL).

### Changed

- `data/levels.bin` loader (`load_levels`) reads the FLVL 0.7.28 base-stat
  trailer into `LevelTable::base[]`; `baseStat(L)` accessor added (back-tolerant:
  falls back to a coarse level proxy on an older bundle).

## [0.1.0] - 2026-06-13

Inaugural versioned release. At this point FFSmith already plays a **vertical
slice of the real game**: title menu → New Game → intro cinematics → the retail
opening cutscene chain → walkable fields with real tiles/collision/sprites/
animation → menus with real data → turn-based ATB battles with real stats,
rewards and level-ups → save/load. The work below predates formal versioning
and is captured here as the 0.1.0 baseline.

### Added

- **Host + main loop (M0).** SDL2 window, fixed-timestep logic loop
  (`Host::frame`), input edge-detection, headless `--frames` mode.
- **Static map render (M1).** Loads toolkit-baked `.ffmap` + `.tex` and
  composes them **byte-identically** to the toolkit render (`compositor.cpp`
  reproduces the PIL div255 path; verified 0-diff on g0_p0_m101 and m501).
- **Field movement, camera & collision (M2).** One-tile-stepped walking,
  facing, a follow-camera clamped to map bounds, and wall/object collision
  from decoded `capk.dat` passability (`Field::isSolid`).
- **Event VM, NPCs, dialogue, triggers, warps (M3).** A script VM over the
  baker's structured events; solid NPCs and face-to-talk; step-on triggers;
  cross-map warps (`0x41 MapChange` and the door `0x6b`/`0x66` idiom); real
  on-screen, word-wrapped, per-area dialogue from the `msg{N}` banks.
- **Field sprites + animation (M3b).** Player and NPCs render as real `fldchr`
  sprites (feet-aligned, transparent), facing correctly and animating while
  walking, using per-sprite `field_anm` geometry; cycle-less object sprites
  (chests/doors/crystals) draw their real frame; overhead z-order split at the
  baked per-map threshold.
- **Game state machine (M4).** Top-level mode machine (Title/Field/Battle with
  Menu/Dialog overlays) booting to an interactive main menu; New Game →
  intro cinematics (prologue crawl, logo, chapter card) → field; Continue;
  Debug launcher.
- **Menu pages — Item / Equip / Status (M5, core).** Real decoded data: a
  scrollable item list with descriptions, per-character equipment resolved to
  item names, and stat pages; item-use (heal) and actionable equip-swap by
  baked `item_type` category.
- **Turn-based battle (M6, core).** Battle mode vs. real monster groups (1–3
  enemies) with real decoded stats; ATB ordering by SPD, target select,
  Attack/Defend/Run, a **Magic** command backed by the real spell table, the
  decoded `CalcPhysicAttackDmg` core wired to real BTLACT inputs, and
  **EXP/gil rewards + level-ups + revive** on victory.
- **Save / load (M7).** FFSmith's own `FSAV` format (current v5) persists map +
  player tile + facing + sprite, the party (current HP/MP), inventory, gil, and
  a ~4 KB script-state blob; round-trips verified. (This is *not* the original
  `save.bin` format.)
- **Audio (M8).** Per-map field and battle BGM, title BGM, and decode/confirm
  sound effects, played from the baked OGG banks.
- **Event VM v2.** Branching (`0x3d` if-not-goto), flag/var banks, appear
  conditions, choices (`0x3c`), `CallEvent` (`0x66`), auto events and the
  common-event pool — the retail intro chain plays end-to-end headless.
- **Scripted battles.** `0x50 ScriptEncount` decoded and implemented: real
  formation tables (FENC) + real monster stats (FMN2), VM pause → battle →
  resume, with the result readable by scripts.
- **Cutscene direction.** An actor system with the decoded 68-entry `0x68`
  command table, call-stack script suspension, eased camera pans, screen
  fades and teleports/visibility ops; the retail intro plays headless with
  full direction through the prologue battle.
- **Real data tables consumed.** The 251-spell magic table, item equip stats
  (weapon ATK / armor DEF), and per-job HP%/MP% growth multipliers are baked
  by the toolkit and consumed in battle/menus (replacing earlier heuristics).
- **Headless self-tests.** Deterministic CLI checks for the above —
  `--frames`, `--walk`, `--events`, `--vmtest`, `--enctest`, `--cuttest`,
  `--battlesim`, `--leveltest`, `--revivetest`, `--menutest`, `--itemtest`,
  `--equiptest`, and the `--*shot` screenshot modes.

### Notes

- Several systems are still **approximated** at 0.1.0 — the ATB/turn scheduler,
  the PRNG, the damage-formula modifier tail, the per-step encounter roll, and
  consumable use-effect magnitudes among them. The authoritative, always-current
  list lives in `docs/architecture/ffsmith_status.md` ("Approximated /
  heuristic" and "Missing entirely"); the engine roadmap's N-series tracks
  closing them.
- This release establishes versioning only; it changes no runtime behavior.
  `src/version.h` is the canonical version and `CMakeLists.txt` mirrors it.
