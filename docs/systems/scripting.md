# System: Script State (flags, variables, references, appear)

*Snapshot 2026-06-10. Source: `src/field/script_state.{h,cpp}`. RE'd 2026-06-10 from `SetReferenceFlag`/`IsFlagEnabled` (c:134634), `GetVariable`/`SetVariable` (c:133737), `GetReference` (c:134452), `CheckCondition`, `CheckEventAppear` (c:137096; readable FFV twin c:168498).*

## Banks (HIGH — offsets cited in script_state.h header)

Flags (bit-addressed, word = idx>>5, bit = idx&0x1f):

| Type | Original location | Size | FFSmith |
|---|---|---|---|
| 0 | `Field+0xe478` | 128 bits | `f0[4]` |
| 1 | `+0xe494 + page·0x40` | 512/page | `f1[8][16]` |
| 2 | `+0xe488` | 96 | `f2[3]` |
| 3 | `+0xe614 + page·0x40` | 512/page | `f3[8][16]` |
| 4 | `+0xe794 + page·4` | 32/page | `f4[8]` |
| 5 | `GameClass+0x1a174` (global) | 32 | `f5` |

Variables (i32): type 0 = 128 @+0xe7ac; type 2 = **the** script-var bank, 512 @+0xe9ac; type 3 = 24 @+0xf1ac; type 1 = alias into bank 2 at `[0x20 + page·0x50]`; type 4 = 8/page @+0xf20c. Type 5 = system specials: 0 msgBank, 2 page (`+0xe474`), 3 storyState (`+0xe464`), 4 main-mode (FFSmith hardcodes 3 = field — MEDIUM, wrong during battle/menu reads), 5/6/7 misc, default path returns 1.

Page count: real engine uses 6 page slots; FFSmith allocates 8 (`PAGES = 8`, headroom). Page semantics beyond indexing are **partial** (open question).

## GetReference targets

Implemented: 1 flag, 2 (idx≠0), 3 variable, 5 party-has-member (via `VMEnv.partyHas`), 7 item count (`VMEnv.itemCount`), 9/10 immediate, 0xf random-in-range (span×10/÷10 quirk preserved), 0x10 packed `type | p2<<0x14 | idx<<8`. Unhandled targets (chara/event/etc.) log and return 0 — **known gap**, affects scripts that branch on actor positions.

`check_condition` ops: 0 ==, 1/8 !=, 2 >, 3 <, 4 >=, 5 <=, 6 bitwise-AND≠0, 7 either≠0.

## Appear conditions (HIGH)

31-byte block, slots at offsets {0,5,10,19,22,25}, present when first byte ≠ 0:
slot 0/1 flag (type, bit BE16, expected), slot 2 variable (type, idx, BE32 value, op), slot 3 item owned, slot 4 member in party, slot 5 timer window (15-tick window ops 0–5). Verified on real data: m501's stacked doors switch on global flag5 bit 10; Barbara NPC story-gated.

## Persistence

`serialize()` → `"SST" + ver 1` + fixed-order LE u32 dump of every bank + specials + timer = **4,032 bytes**, embedded in `FSAV` v5. `deserialize` validates exact size + magic. Self-tested round-trip (`--vmtest`) and against an independent Python parser.

## Caveats

- `getFlag` uses a `const_cast` helper — safe but ugly (refactoring candidate).
- `dirty` flag drives parallel-event re-scan; it is FFSmith plumbing, not an original mechanism (the original re-evaluates appear via `UpdateEventAppear` — behaviorally equivalent intent, MEDIUM).
- Timer (`this+4`) is settable by op 0x06 but **nothing ticks it** in FFSmith yet — timer-based appear slots will never trigger (gap).
