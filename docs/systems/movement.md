# System: Movement

*Snapshot 2026-06-10 (rev. 2026-07-11). Source: `src/field/field.cpp`. Original references: `FieldClass::MoveChara*` family, `MovePlayer` (c:113088), `GetAfterPositionOfWalk` (c:118468) — see `ENGINE_RE_ROADMAP.md` Part 4 for the decoded call graph.*

## Model (HIGH for FFSmith's own behavior; MEDIUM as a match to the original)

Tile-stepped movement with pixel interpolation:

- State: `col_/row_` (current tile), `tcol_/trow_` (target), `prog_` (pixels into the step), `speed_ = 2` px/tick, `facing_`, `moving_`.
- Input → direction priority Up > Down > Left > Right (`dirFromHeld`). Facing updates even when the move is blocked.
- A step commits only when `!isSolid(target)`; mid-step input is ignored until arrival.
- `pixelX/Y = tile·cell + DX/DY·prog_` — the camera and sprite draw from this, so motion is pixel-smooth.
- Walk animation: quarter-step phase → frame sequence `{walkA, idle, walkB, idle}` (`animCol`).
- On arrival: step/range triggers fire (`stepTriggerAt` → boot 2/3/6/7 rect events).

## Deliberate simplifications vs the original

- ~~One actor.~~ **NPC movement implemented** (script-driven 2026-06-11; autonomous wander 2026-07-11, FFSmith 0.3.0). `Field::tickWander` follows `MoveCharaAuto` (c:115518): a move_type-2 NPC picks a random direction whose target stays inside its event rect (`GetPassFlags` c:117339); a collision-blocked pick only turns the NPC; walk/pause cadence from the `field_constant.dat` tables (@0x37 by speed, @0x42 by frequency; decoded defaults compiled in, `data/field_constant.bin` overrides). Spawn = rect origin + FFM6 `off_x/off_y`, initial facing applied. Remaining approximations (MEDIUM, flagged in code): all wander pauses while any script/dialogue is pending (original stops only the engaged NPC via `CheckEventActive`); blocked-pick turn reuses the wander pause (face-command duration undecoded); `MoveCharaControl` (player) and `MoveCharaPassiveHit` touch-boot remain unported.
- **Speed:** `speed_ = 2` px/tick at 32-px tiles = 16 ticks/step. **LOW confidence** this matches the original walk rate; never traced against the real game (the roadmap's golden-trace plan, §4.4 step 6, was not executed).
- **No wrap:** map edges clamp; the original wraps when the per-map wrap flags (`record+3/+4` in `CheckMovePass`) are set. Wrap flags are not currently baked into `.ffmap` (the FFM0 spec reserved header bits for them, but the baker never wrote them — see contradiction report).
- **No diagonal/run/jump/fly:** `MoveCharaActionOfJump/OfFly` unported.

## Verification

`--npctest` (8 checks): synthetic-map wander — rect confinement, wall/player-tile avoidance, cadence bounds, move_type-1 NPCs stand still. Live-map verification: all 7 g0_p0_m500 wanderers stayed inside their decoded rects over 900 ticks with table-exact waits. `--walk URDL.C` headless traces print per-step `(col,row,facing,moved|blocked)`, follow warps, pump autos on `.`, confirm on `C`. Bounds-block, wall-block, facing, and warp round-trips (m500↔m501) verified this way.
