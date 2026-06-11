# Engine Roadmap (post-audit, 2026-06-10)

*Supersedes the milestone table in `ENGINE_RE_ROADMAP.md` Part 5 (kept for its RE reference material). North star unchanged: **FFSmith playable through the story.***

## Done (see architecture/ffsmith_status.md for the ledger)
M0 loop · M1 byte-identical map render · M2 movement/collision/camera · M3 VM/NPCs/dialogue/sprites/triggers/warps · M4 state machine · M5 menus (core) · M6 battle (core) · M7 save/load (FSAV) · M8 audio · Event VM v2 (branching/flags/appear/autos/common pool) · retail intro chain plays to the prologue battle.

## Next milestones (recommended order)

| # | Milestone | Why now | Key RE targets |
|---|---|---|---|
| N1 | ~~ScriptEncount + scripted-battle hand-off~~ **DONE 2026-06-10** (`--enctest`; remaining: bsc.dat battle scripts, m200-chain verification on Windows) | — | — |
| N2 | **Battle fidelity pass 1** | Damage numbers are the most visible inauthenticity | `SetJobStatus` stat derivation; hit-count/crit/element modifiers; original RNG; real enemy SPD/turn model |
| N3 | **Random encounters** ◑ areas+formations decoded/baked (FFM5/FENC), approx roll behind `--encounters` | finish: the original per-step roll formula | roll location in FieldClass; floor-attr 15 link |
| N4 | **NPC movement** | Towns look dead; cutscenes need actor moves | `MoveCharaAuto`/`MoveCharaEvent`, NPC-move opcodes |
| N5 | **Story persistence completeness** | Long play sessions | common parallels 0x102/0x105/0x106, page register semantics, timer ticking, msgBank override |
| N6 | **World map presentation** | The real opening look | perspective/tilt renderer, Far background, wrap-aware movement/camera (needs wrap flags baked — toolkit change) |
| N7 | **Menus pass 2** | Parity | Magic/abilities page, shops (`PurchaseScene`), NameInput, save slots |
| N8 | **Audio tail** | Polish | data-driven battle SFX, victory jingle, confirm title BGM id |

## Standing guardrails

- Decode in the toolkit first, bake, then consume (asset-pipeline rule 4).
- No guessed decodes; manual annotation > heuristics; eyeball-verify renders.
- Engine-only changes don't bump the toolkit version; FFM bumps require engine rebuild before rebake.
- Add a self-test with every milestone (testing_strategy.md).
