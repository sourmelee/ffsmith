# Engine Refactoring Candidates

*Audit 2026-06-10. Ranked. Each is sized assuming the current ~3.6K LOC codebase.*

1. **Split `Host`** (large, do incrementally). Natural seams already visible in the code:
   - `game/battle.{h,cpp}` — Combatant, phases, ATB, damage, rewards (~450 LOC lift, self-contained: touches Host only via gameParty_/items_/audio).
   - `game/state.{h,cpp}` — GameMember/InvSlot/inventory/gil/newGame/useItem/equip logic (the persistent `GameState` the memory notes already name).
   - `ui/menu.cpp`, `ui/title.cpp`, `ui/debug_launcher.cpp` — pure render/update pairs.
   - Keep `Host` as window/loop/input/audio/mode-dispatch only (~the M0 core).
   Payoff: battle exact-match work (the next big RE push) gets a testable module.
2. **Move map+Field ownership from `main.cpp` into Host** (small). Kills the raw-pointer triangle, makes warp logic a Host method, shrinks main to CLI+wiring.
3. **Extract `game/save.{h,cpp}`** (small): FSAV read/write + version table; main.cpp keeps only the CLI flags.
4. **RNG service** (small): `struct Rng { uint32_t state; int next(int n); }` injected into VMEnv + battle; default seeded, `--seed` flag for deterministic sims.
5. **`approximations.h`** (tiny): centralize every "not yet decoded" constant with a comment citing the open question. Greppable debt.
6. **Event/boot-condition policy object** (medium): `Field` currently encodes boot semantics in four static predicates + enterMap/pumpAuto/rescanParallel. When boots 2/3 and common parallels get RE'd this will churn; isolating "what runs when" into one table-driven unit will keep Field readable.
7. **Text/i18n layer** (large, later): glyph atlas abstraction so the ASCII path and a future CJK path coexist; prerequisite for non-English play.
8. **Compositor → tile renderer** (later, gated on world-map milestone): replace whole-map texture with per-frame visible-tile drawing; also unlocks palette/fade effects without re-uploads.

Non-candidates (deliberately fine as-is): the `bundle.cpp` loader style (flat, boring, correct); compositor div255 math (must stay bit-exact); self-test flags living in the binary (they're the test harness).
