# System: Movement

*Snapshot 2026-06-10. Source: `src/field/field.cpp`. Original references: `FieldClass::MoveChara*` family, `MovePlayer` (c:113088), `GetAfterPositionOfWalk` (c:118468) — see `ENGINE_RE_ROADMAP.md` Part 4 for the decoded call graph.*

## Model (HIGH for FFSmith's own behavior; MEDIUM as a match to the original)

Tile-stepped movement with pixel interpolation:

- State: `col_/row_` (current tile), `tcol_/trow_` (target), `prog_` (pixels into the step), `speed_ = 2` px/tick, `facing_`, `moving_`.
- Input → direction priority Up > Down > Left > Right (`dirFromHeld`). Facing updates even when the move is blocked.
- A step commits only when `!isSolid(target)`; mid-step input is ignored until arrival.
- `pixelX/Y = tile·cell + DX/DY·prog_` — the camera and sprite draw from this, so motion is pixel-smooth.
- Walk animation: quarter-step phase → frame sequence `{walkA, idle, walkB, idle}` (`animCol`).
- On arrival: step/range triggers fire (`stepTriggerAt` → boot 2/3/6/7 rect events).

## Deliberate simplifications vs the original

- **One actor.** Only the player moves. The original ticks every actor through the `MoveChara` state machine (Control/Auto/Event/Wait/Hide); NPC autonomous walk (`MoveCharaAuto`) and script-driven movement (`MoveCharaEvent`, NPC-move opcodes) are unimplemented — NPC-move script ops are log-skipped.
- **Speed:** `speed_ = 2` px/tick at 32-px tiles = 16 ticks/step. **LOW confidence** this matches the original walk rate; never traced against the real game (the roadmap's golden-trace plan, §4.4 step 6, was not executed).
- **No wrap:** map edges clamp; the original wraps when the per-map wrap flags (`record+3/+4` in `CheckMovePass`) are set. Wrap flags are not currently baked into `.ffmap` (the FFM0 spec reserved header bits for them, but the baker never wrote them — see contradiction report).
- **No diagonal/run/jump/fly:** `MoveCharaActionOfJump/OfFly` unported.

## Verification

`--walk URDL.C` headless traces print per-step `(col,row,facing,moved|blocked)`, follow warps, pump autos on `.`, confirm on `C`. Bounds-block, wall-block, facing, and warp round-trips (m500↔m501) verified this way.
