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

## [0.3.0] - 2026-07-11

### Added
- **NPC auto-wander** (`Field::tickWander`) — the `FieldClass::MoveCharaAuto`
  (c:115518) model: a `move_type`-2 NPC picks a uniformly-random direction among
  those whose target tile stays **inside its event rect** (`GetPassFlags`
  c:117339 low bits) and walks there; a collision-blocked pick only *turns* the
  NPC (the hit bits' face command `0x10|dir`). Walk/pause cadence comes from the
  `field_constant.dat` tables — walk `cfg[0x37+speed]` ticks/step, pause
  `cfg[0x42+freq]` ticks (`CalcCharaAnimeSpeed` c:118074 / `SetCharaAction`
  c:117759) — via the new `FieldConstant` loader (`data/field_constant.bin`,
  decoded retail defaults compiled in). `move_type` 3 (wander-unbounded) is
  supported but unused in retail data (all 370 wanderers are type 2).
- **FFM6 loader** — per-event 7-byte NPC movement block (`move_type`, `facing0`,
  `chflags`, `speed0`, `off_x`/`off_y`, `freq0`; `InitEventDataOfChara`
  c:119752). NPC spawn tile is now `rect origin + offset` and initial facing is
  applied (previously every NPC spawned at the rect origin facing down —
  m500's ev18 was 4,2 tiles off).
- **`--npctest` self-test** — synthetic-map wander verification: rect
  confinement, wall/player-tile avoidance, field_constant cadence bounds,
  `move_type`-1 NPCs never move (8 checks).
- **`--fieldshot` accepts `--frames N`** — builds actors and ticks the field N
  times before the capture, so wander/tile animation can advance headlessly.

### Changed
- **Talked-to NPCs turn to face the player** (SetCharaLookDir on talk; MEDIUM —
  the exact original call site is unconfirmed). Only the engaged NPC stops
  wandering in the original (`CheckEventActive`); FFSmith currently pauses all
  wander while any script/dialogue is pending (approximation, flagged in code).

## [0.2.0] - 2026-06-15

### Fixed
- **Field objects only auto-animate when flagged.** `drawSprite` now cycles
  frames solely for `isObject==2` (ambient effects: fire/flames); state sheets
  (`isObject==1`: doors/chests/props) hold frame 0, fixing 2-state doors that
  flickered or drew their whole sheet. Object draw is now one path: crop `frame[fi]`
  from the `var`-selected texture and place it at `(tile-centre+px, tile-bottom+py)`
  using the baked FFD part offset. Sibling files are palette variants (no file-
  cycling); only `isObject==2` in-sheet effect strips animate — fixes large objects
  (96×96 tree) sitting too far left and palette-flicker on multifile sprites. New `directional` sprite mode (6) for vehicles: `drawSprite`
  picks the down/up/side frame by `facing` (RIGHT = side flipped), so the airship
  turns with movement; the propeller animates (2 frames per direction).

### Added
- **Animated field-object sprites.** `SpriteGeo` now carries a per-sprite frame
  list + `mode` (char / static / grid / multifile / special / battlechar), parsed
  from the new `spritegeo.bin` **`FSG2`** format (`load_spritegeo` still reads the
  old `FSGE` for back-compat). `Host::drawSprite` cycles grid/multifile frames at
  ~7 fps: `grid` crops in-sheet rects, `multifile` swaps to sibling
  `fldchr{img}_{k}` textures, each centred bottom-on-tile by its own size; `static`
  keeps the legacy authored anchor (the `sprite_grid.json` contract).

### Fixed
- **Field objects no longer mis-aligned.** Geometry is keyed by sprite img id, so
  doors / crystals / chests / effects draw their real frames instead of a
  mis-keyed field_anm sub-rect. Battle-character sheets (fldchr30–49) are flagged
  `battlechar` and routed to the 48×48 grid path rather than animating as props.


## [0.1.3] - 2026-06-15

### Added

- **Monster battle sprites.** Enemies now render as their real FFD sprites,
  `tex/mon{group}_{variant}.tex` where **group = BE-u16 `body[56..57]`** and
  **variant = `body[58]`** (`SetBtlEnemyModel` `mon%d_%d`). Each `(group,variant)`
  is ONE static image -- the `mon{N}_{M}.png` "variants" are distinct monsters /
  recolours, not animation frames -- drawn with a gentle idle bob (no frame-cycling).
  Positioned in the battle scene with name + HP bar + target caret; falls back to
  the text box if missing. `Monster`/`Combatant` carry the group/variant; `monTex`
  caches per `(group,variant)`. *(Earlier same-day work mis-read `body[0]` as the
  sprite set, which flickered through palette variants -- corrected here.)*

## [0.1.2] - 2026-06-14

### Added

- **UI polish pass.** Text now has a 1px **drop-shadow** (`drawText`, the classic
  FF look, toggleable). Gil/EXP use thousands separators (`commafy`). A `drawGauge`
  helper draws coloured **HP/MP bars** (green->yellow->red / blue) in the menu party
  panel, the Status page, and battle. **Config** is now a settings list -- aspect,
  **window opacity**, **window colour** (6 presets), text shadow, and **message
  speed** -- mirrored in the Debug launcher. Dialogue has a **typewriter** reveal
  (first confirm completes it) and a warm tint distinct from the cool narration
  telop. Battle gained a `drawWindow`-skinned command window, party HP/MP gauges,
  and **floating damage / heal numbers**. (Monster/character *battle sprites* still
  need a toolkit baking pass -- the monster table has no sprite id and no monster
  textures are baked yet; tracked as a follow-up.)

- **FFD dual Light/Dark party system (5 members each).** Two parties -- Warriors
  of Light (0) and Darkness (1), up to 5 active each -- with a side selector, after
  libjniproxy `GetPartyMemberID` (side @+0x21424, member-id arrays @+0x21428) and
  the entered-roster mask @+0x2172c. `parties_[2]` + `partySide_`; `gameParty_` is
  the active side's working copy, synced via `commitActiveParty`; `switchSide`
  swaps. `newGame` seeds both sides; battle write-back, level-ups and item-use all
  stay on the active side. **FSAV bumped to v6** (persists both parties + active
  side; v5 saves still load into Light).
- **FFD-accurate main menu.** Root command list is now the full FFD set -- Item,
  Magic, Equip, Job, Status, Formation, Config, Save (+ Quit) -- skinned with the
  FFD window. Magic and Job appear as labelled stub panels; Item/Equip/Status/
  Config/Save are live. The root menu shows an active-party panel (member name /
  level / HP / MP) headed *Warriors of Light* or *Darkness*.
- **Formation tab + debug party control.** The Formation page reorders (hold/swap),
  adds/removes members from the character roster, and switches the active side
  (Left/Right). The Debug launcher gained a **Party** side toggle (Formation is the
  full builder).

### Fixed

- **Screen no longer stuck black after a door/map warp.** Fades are Host-owned
  and persist across warps (for the fade-out -> warp -> fade-in door idiom), but
  the warp is handled outside the script VM so the fade-IN half never ran. A
  completed door warp now ramps the fade back in -- but only when the destination
  map issued no fade of its own (`fadeGen()` check), so real cutscene blacks are
  respected. (`main.cpp` warp path, `Host::fadeInAfterWarp`/`fadeGen`.)

### Changed

- **FFD window skin for menus + dialogue.** `Host::drawWindow` reproduces
  `GameClass::DrawWindow` (libjniproxy 153133-153153): a 3-stop vertical blue
  gradient (top RGB 63,69,134 -> mid 42,43,102 -> bottom 75,78,122 at alpha 0xa0)
  with a light frame, replacing the flat rectangles on the command list, the
  Item/Equip/Status pages, and the message box.
- **Resolution / aspect-ratio option with reflow.** A new in-game **Config** menu
  page picks an aspect ratio (Free-form, 4:3, 5:4, 16:9, 16:10, 1:1, and vertical
  9:16 / 10:16 for a future portrait/Android layout); `Host::setAspect` resizes the
  window and the UI/HUD/field viewport reflow from the live window size (no
  letterbox). Also a `--aspect W:H` CLI flag applied at startup, and the **Debug
  launcher** gained live **Aspect**, **Encounters** and **Fullscreen** (desktop)
  toggles alongside Scale. Free-form (the current resizable behaviour) stays the
  default. *(Persistence across launches is a follow-up.)*
- **Opening narration renders as a full-screen telop, not a dialogue box.** Op
  `0x01` ScriptSentence is the FieldClass *Sentence* system (libjniproxy
  `InitSentence`/`DrawSentence` -- accumulating on-screen lines over the scene),
  distinct from op `0x00` SetMessage (windowed). The VM now routes `0x01` to a
  new full-screen narration overlay (`Field::inSentence`/`sentenceLines`,
  host render) instead of the message box, so the prologue reads as the classic
  blue narration screen. (`event_vm.{h,cpp}`, `field.{h,cpp}`, `host.cpp`.)

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
