# FFSmith — Engine State Report

*Audit snapshot 2026-06-10 (commit `d2aacc2`, 34 commits since 2026-06-01). This is the authoritative "where is the engine actually at" document. Confidence: HIGH = verified in code + self-test; MEDIUM = implemented but approximated or unverified against the original; LOW = guess/placeholder.*

## Maturity summary

FFSmith is **~10 days old and already plays a vertical slice of the real game**: title menu → New Game → intro cinematics → the retail opening cutscene chain (m0 → m101 → m100 → m101 → m200 → m1 → m300, ending at the scripted prologue battle) → walkable fields with real tiles/collision/sprites/animation/z-order → menus (Item/Equip/Status with real data and working actions) → turn-based ATB battles with real stats, rewards, level-ups → save/load. It is **not** yet playable through the story: the scripted-battle hand-off (`0x50 ScriptEncount`), random encounters, AP/abilities, shops, and the perspective world-map renderer are missing.

## Milestone ledger

| Milestone | Status | Evidence |
|---|---|---|
| M0 host + loop | ✅ HIGH | `Host::frame` fixed-timestep; `--frames` headless |
| M1 static map render | ✅ HIGH | byte-identical vs toolkit (g0_p0_m101, m501); `compositor.cpp` reproduces PIL div255 |
| M2 movement + camera + collision | ✅ HIGH | `Field::update`, `isSolid` (capk pass nibble), clamped follow camera; `--walk` traces |
| M3 event VM, NPCs, dialogue, triggers, warps | ✅ HIGH | `event_vm.cpp`, FFM2+ events, msg banks; verified m500↔m501 round trip, 4507/4514 warp records in-bounds |
| M4 game state machine | ✅ HIGH | `Host::Mode` {Debug, Title, Field, Battle, Intro}; Menu/Dialog as overlays — mirrors `GameClass::MainFunc` shape |
| M5 menu pages | ◑ MEDIUM | Item/Equip/Status real data + item-use + equip-swap; **no** Magic/Config/Formation/Shop pages |
| M6 battle | ◑ MEDIUM | ATB loop, groups 1–3, target select, Magic, decoded physical formula core, EXP/gil/level-up/revive; large approximation tail (below) |
| M7 save/load | ✅ HIGH (own format) | `FSAV` v5 in `main.cpp`; round-trips verified incl. ~4 KB script-state blob. **Not** the original `save.bin` format |
| M8 audio | ✅ MEDIUM | per-map field/battle BGM, title BGM, "decide" SE; `titleBgm_ = 18` is an unconfirmed placeholder (host.h:149) |
| Post-M8 Event VM v2 | ✅ HIGH | branching (`0x3d` if-not-goto), flags/vars banks, appear conditions, choices (`0x3c`), CallEvent (`0x66`), auto events, common pool — `--vmtest` 11/11 |

## What is real vs approximated

### Decoded-and-faithful (HIGH)
- Map composition, tile-word slot dispatch, zero-skip, TS 16/32 heuristic (`compositor.cpp`).
- Collision pass nibble from `capk.dat` word A & 0xF; damage-floor bit 0x10 of `(A>>18)&0x3f`; tile-animation bits 8–17 (frames/speed/type).
- Overhead z-order split at the baked per-map threshold (`FieldClass+0xdc2c`).
- Event-script execution model: block registry semantics, BE operands, var-indirection masks, `ScriptIf` jump-on-fail, `0x41` map/layer/x/y/dir, `0x6b` BulkSetVars, `0x66 CallEvent` + common event 0x104 door idiom, `CheckEventAppear` 6-slot gate (`script_state.cpp:183`).
- Flag/var bank layout (sizes, paging, type-5 system specials) from `SetReferenceFlag`/`GetVariable` (script_state.h header comment cites the c-file lines).
- Physical damage core: `(W<<6·H>>5 + spread − D<<6) × (3A+L) / 1024` per `docs/BTLACT_MAP.md` (`Host::physDamage`).
- Monster EXP/gil offsets (body[6]/body[10] BE u32), §8 EXP thresholds + HP/MP growth (`levels.bin`).
- New Game start: boot_data §1 scenario record 0 → map 0, map-default spawn (FFM4 header).
- field_anm character grid (48×48, pitch 50, rows=facing, Right=Left-flipped) and per-sprite object geometry (`spritegeo.bin`).

### Approximated / heuristic (MEDIUM — flagged in code comments)
- **ATB**: gauge += SPD per step, threshold 256, random initial stagger (`pickNextActor`). Original turn scheduler not decoded.
- **Magic**: spell knowledge = `INT≥2`/`MND≥2`, damage `power + INT·3 − def/4 ± rand(4)` — NOT the decoded `CalcMagicDmg`.
- **Damage formula tail**: crit/element/race/status/hit-count/back-attack/defense% modifiers absent; job-derived attack stat `A` uses raw STR; enemy `W = 5 + level` is invented.
- **Encounters**: `startBattle` picks a weak lead + difficulty-band extras (atk/hp bands). Real encounter tables (`encount_ratio` is baked but unused) not implemented. Enemy `spd = 7` fixed.
- **Run** = 50% coin flip; **Defend** = damage/2.
- **Item effects**: parsed from description text (digits → heal amount; "Knocked Out" substring → revive). The real item-effect table is undecoded.
- **Damage floors**: drain `maxHP/16` per step — exact original amount not decoded (host.cpp:169 comment).
- **Tile-anim speed**: `ticks/frame = speed·8` — exact speed table not decoded (host.cpp:152).
- **EXP split**: full EXP to every survivor (original may divide); reward rate fixed at 100%.
- **Starting inventory/gil**: hardcoded `{Potion×5, Phoenix Down×2, …}, 500 gil` in `newGame()` — not from game data.
- **Level-ups**: HP/MP only; attributes don't grow; per-job HP%/MP% absent.
- **Choice menu text**: option *value* treated as a message id (event_vm comment "0x3c choice menu: each option value is a message id") — provisional; real choice-line source still open.
- **Boot conditions 2/3**: treated as step triggers "pending their trigger-type RE" (field.cpp:29).

### Missing entirely
- `0x50 ScriptEncount` scripted-battle hand-off + post-battle script resume (the current frontier; intro chain stops here).
- Random encounters, AP/ability learning, status effects, elemental/crit modifiers, shops, NameInput UI, fades/palette ops, NPC scripted movement (`MoveCharaEvent`) and autonomous walk (`MoveCharaAuto`) — NPCs render statically.
- Common parallels (0x102/0x105/0x106…), page register (0xe474) full semantics, op 0x32 message-wait.
- Perspective/tilted world-map renderer + Far background (m1 flyover renders flat); FFD title-logo asset (bundle ships the FFL logo).
- Original save format (`GameClass::SaveGameData` → `save.bin`, 15 KB/slot) — FFSmith uses its own `FSAV`.
- Battle SFX (data-driven per weapon/spell), victory jingle (`ReserveJINGLE`), Mobile MFi→MIDI audio.

## Open questions blocking the next milestones

1. **ScriptEncount semantics** — formation id source, post-battle destination (common 0x101 path observed → m150). Frontier as of 2026-06-10 roadmap status.
2. **Real turn scheduler / RNG** — original PRNG unidentified (roadmap Part 6 risk; needed before battle exact-match).
3. **Job stat derivation** (`SetJobStatus`) — required for damage exact-match; BTLACT map exists, derivation doesn't.
4. **Timing model** — assumed 60 Hz frame-locked; never confirmed against the original (roadmap Part 6 risk, still open).
5. **Title BGM id** — placeholder 18, confirm by ear.
6. **Real opening world-map presentation** — needs the perspective renderer.

## Documentation drift found in this repo (see ../development/contradiction_report.md)

- `README.md` contains stale "Next:" markers (e.g. "Next: EXP/gil battle rewards" while a later bullet says rewards are done) and describes the save as FSAV v2/v3 in places; code writes **v5**.
- `ENGINE_RE_ROADMAP.md` Part 5 table says "M2 is the immediate objective" — superseded by the appended status log (M0–M8 done).
- `docs/ASSET_PIPELINE.md` bundle layout (JSON data files, FFM0 ffmap) is the original draft; actual bundle is FFM4 + binary `.bin` tables.
