# Engine Contradiction & Drift Report

*Audit 2026-06-10. Each entry: problem → evidence → suggested resolution. Toolkit-side report: `../../../Python/docs/reverse_engineering/contradictions.md`.*

## 1. README.md is an append-only changelog wearing a status hat
**Problem:** Multiple bullets carry stale "Next:" markers and superseded facts. Examples: the bullet list ends "**Next:** EXP/gil battle rewards + level-ups, then encounter tables" while the M6 bullet above it already says rewards/level-ups/revive are done; M7 describes `FSAV` v2 then v3 in different bullets.
**Evidence:** `Engine/README.md` Status section vs `main.cpp:192` (`uint8_t ver = 5`).
**Resolution:** Rewrite README Status as a short table pointing at `docs/architecture/ffsmith_status.md` (this audit) instead of appending bullets per milestone. Keep history in git, not prose.

## 2. ENGINE_RE_ROADMAP.md Part 5 contradicts its own appended status log
**Problem:** "M2 is the immediate objective; everything in Part 3 precedes it" (Part 5) vs the trailing status paragraphs documenting M0–M8 + Event VM v2 complete.
**Resolution:** Mark Part 3–5 as historical ("kickoff plan, superseded by the status log / ffsmith_status.md"). The decoded call graphs in Part 4 remain valuable reference — keep them.

## 3. docs/ASSET_PIPELINE.md "Bundle layout (v0 draft)" is two generations stale
**Problem:** Describes `assets/baked/` with `data/items.json`, `text_{lang}.json`, FFM0 with `flags u16 bit0 wrap_x/bit1 wrap_y`, and a to-do list ("thin writer for the flat record") long since done.
**Evidence:** Actual bundle (toolkit `ffsmith_bake.py` + `bundle.cpp`): binary `data/*.bin` (FITM/FCHR/FMON/FSPL/FLVL/FSGE/FCAN/FCFL/FSTR/FINT/FAUD), `text/msg{N}.bin`, FFM4 with a packed reserved u32 (threshold/field_bgm/battle_bgm) and **no wrap flags anywhere**.
**Resolution:** Keep the Principle + Symbiosis sections (still accurate); replace the layout section with a pointer to `Python/docs/architecture/asset_pipeline.md` (the authoritative spec written by this audit). Also note: wrap flags were planned, never baked — they're a real future need for world maps (movement.md).

## 4. Save-version drift across docs
**Problem:** README (v2/v3), roadmap statuses (v3/v4/v5), memory notes (v4/v5) — only the latest is right.
**Evidence:** `main.cpp` writes ver 5; reader accepts ≥2 features progressively.
**Resolution:** Single source of truth: `Python/docs/formats/saves.md` (FSAV v1→v5 history table).

## 5. Planned source layout vs actual
**Problem:** Roadmap §2.1 and README "Source layout" promise `src/mtx/`, `src/game/`, `src/battle/`, `src/menu/`, `tests/`, `third_party/`; none exist. README's own layout block lists `src/mtx/ (later)` and `src/game/ (later)` which is honest, but battle/menu/state-machine code silently landed in `src/host/host.cpp` (81 KB).
**Resolution:** Either split Host (see refactoring_candidates.md #1) or update the layout docs to bless the current shape deliberately.

## 6. `0x41 MapChange` operand comment drift
**Problem:** `event_vm.h` history and older docs describe the door warp as "0x66 SetEntityAction action 0x04" and 0x41 operands as map,x,y,dir,sub.
**Evidence:** Corrected 2026-06-10 (toolkit 0.7.25): 0x66 = CallEvent(id, block); 0x104 is the common move-map routine; 0x41 = map/**layer**/x/y/dir. `event_vm.cpp` implements the corrected form; README's M3b(doors) bullet still teaches the obsolete "SetEntityAction" reading.
**Resolution:** Annotate the README bullet or replace per #1.

## 7. Dead/unused code & data
- `VMOut.bgm` (PlayBGM ops) is produced but never consumed by Host — script-driven BGM changes are silently ignored.
- `Field::moveDir_` is written, read only by pixel interpolation — fine; `Event.type` is parsed but unused in logic (boot is the discriminator; type retained for fidelity).
- `FfMap.event` raw region is loaded (legacy FFM0/1 path) but unused now that structured `events[]` exists.
- `ScriptState.timer` settable, never ticked (scripting.md gap).
- `Host::itemIds_` populated, never read.
- `bankOf()` assumes bank = map group `g{N}`; fine for current data, but `msgBank` system var (type 5 idx 0) exists precisely to override this — script writes to it are stored and ignored.
**Resolution:** Each is either a TODO hook (keep, but tracked here) or deletable; decide per item during the next refactor.

## 8. Self-test claims vs binary truth
**Problem:** Docs say "battlesim/menutest/... all green" as of 2026-06-10; nothing in-repo runs them automatically.
**Resolution:** Add a CI-able script (see testing_strategy.md). Until then, treat "all green" claims as snapshots of a manual run.
