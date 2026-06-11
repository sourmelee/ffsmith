# System: Event VM (script execution)

*Snapshot 2026-06-10. Source: `src/field/event_vm.cpp`. Original: `FieldClass::MoveEventScript` (c:138937), `MoveScript` (c:139119/125490), `ScriptIf` (c:134322). Opcode catalogue: `Python/ffd/events/opcodes.py` and `Python/docs/formats/events.md`.*

## Execution model (HIGH — RE'd 2026-06-10, `--vmtest` 11/11)

An event's script is a list of **length-prefixed blocks**; block 0 is the entry. The original advances via a registry (`this+0xf438`): 0/1 = jump to block, 3 = fall through, 4 = end. FFSmith's `run_event_depth` mirrors this with `pc`/`next`:

- Operands are **big-endian** (`GetBuffToWord/Long`); `rdW`/`rdL`.
- **Var indirection**: many ops carry a bitmask; masked operands are indices into var bank 2 instead of immediates (`ReadScriptVariableParamsOfShort` → `ind()`).
- Step cap 2048 blocks per run; CallEvent depth cap 6.

## Implemented opcodes

| Op | Name | Semantics in FFSmith |
|---|---|---|
| 0x00 / 0x01 | SetMessage / ScriptSentence | queue message id (0x01 = cinematic positioned text, treated as dialogue) |
| 0x03 | SetReferenceVariable | `var[vt][vi] = cur (calcOp) GetReference(...)`; calc ops 0..8 = set,+,−,×,÷,%,&,\|,^ |
| 0x04 | SetReferenceFlag | flag set/clear/toggle |
| 0x06 | timer set | `timer = a1·0xf + a0·900` |
| 0x32 | Wait | **skipped** (placeholder; real frame-wait semantics open) |
| 0x35/0x36/0x75 | PlayBGM family | reports track in `VMOut.bgm` (note: **Host does not yet act on it** — BGM is mode/map-driven; LOW-impact gap) |
| 0x3c | MultiChoiceDialog | pause; options = (value, target block) pairs, cancel → next block; resumed via `run_event(ev, st, env, chosenBlock)` |
| 0x3d | ScriptIf | **if-NOT-goto**: two GetReference operands + compare op + target block; jumps on FAIL |
| 0x3f / 0x40 | Jump / RandomJump | jump to block index (random pick among N targets) |
| 0x41 | MapChange | five BE words map/**layer**/x/y/dir with indirection mask → `VMOut.warp*` (layer ignored beyond decoding) |
| 0x57 | ScriptEnd | registry mode 4 |
| 0x66 | CallEvent(id, block) | resolve via `env.findEvent` (map events, then common pool = map 10000); run inline, choice-aware |
| 0x6b | BulkSetVars | sub 2 writes (key,val) pairs into var bank 2 — the door-warp idiom preamble |
| other | — | logged `op 0xNN`, skipped |

`VMOut` carries: messages (in order), human-readable log, warp, choice request, bgm, sawEnd.

## Event triggering layer (`Field`, HIGH unless noted)

- **Boot conditions** (`GetEventBootCondition` / `CheckRangeEvent` c:113368): 0 = auto/on-load (provisionally run once per visit — MEDIUM), 1 = talk, 4/5 = parallels (run whenever appear-conditions pass; re-scanned after flag/var writes via `ScriptState.dirty`), 6 = range-in on step end, 7 = range-in always (fires on map load if spawned inside — this starts the m0 prologue), 8 = confirm-in-rect; 2/3 kept as step triggers **pending RE** (MEDIUM).
- **Appear gate**: 31-byte header block, 6 slots (flag, flag, variable, item, member, timer) — `check_event_appear`, FFV twin c:168498. Gates solidity, talk, render, and auto-run.
- **Loop guards**: ≤4 auto-runs per event, budget 32 per map visit — FFSmith inventions to prevent runaway parallels (not original behavior; MEDIUM).
- **Sequencing**: autos pump one-at-a-time between dialogues; warps held until text is read.

## Known gaps (the current frontier)

- ~~`0x50 ScriptEncount`~~ implemented 2026-06-10: pauses like a choice (`VMOut.hasEncounter` + resume block), Host fights the formation, `Field::resumeAfterBattle` continues; result via GetReference target 8. `--enctest` verifies on real bank-0 data. Open: `bsc.dat` battle scripts, condition-flag table values.
- Common parallels 0x102/0x105/0x106 not pumped; `GetReference` targets chara/event/etc. stubbed (return 0, logged); page register (0xe474) semantics partial; choice-line text source provisional (option value used as msg id); NameInput log-skips.
- **Implemented 2026-06-11 (cutscene direction):** 0x68/0x69 actor command sequences + script suspension (incl. through 0x66 call stacks — `VMOut.resumeStack`), 0x32 timed waits, 0x1b camera re-target, 0x20/0x21/0x55 teleports/visibility/player-set, 0x2a screen fades. `Field` owns an actor per event; `--cuttest` covers move+wait+resume. Remaining: poses, jump arcs (walk-approximated), 0x91, MoveCharaAuto wander.
