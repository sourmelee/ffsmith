# System: Collision & Floor Attributes

*Snapshot 2026-06-10. Source: `src/field/field.cpp:isSolid`, `src/host/host.cpp` (hazards/anim cells), toolkit `ffd/maps/capk.py`. Original: `FieldClass::CheckMovePass` (c:114793), `LoadChipAttribute`, `GetChipAttributeInfo*`, `IsDamagedFloor`.*

## Data source (HIGH)

`capk.dat` (Android OBB): LE u32 TOC; section for tileset `mc_id` at `TOC[mc_id+1]`; each section = u16-BE count + count × 7-byte chip records = u32-BE word **A** + u24-BE word B.

Decoded bits of word A:
- `A & 0x0F` — 4-direction passability mask; 0 = solid all sides. **HIGH** (verified: solids overlay exactly on walls/furniture).
- bit 8 — animated chip; bits 9–10 type (loop/ping-pong); bits 11–14 frame count; bits 15–17 speed. **HIGH** (22,307 overworld water cells ripple).
- `(A >> 18) & 0x3F` — floor attribute enum; **bit 0x10 = damage floor** (`IsDamagedFloor`). Only value 0x12 observed (47 chips, all in dungeon tileset mc63). **HIGH** for the bit, observed-coverage caveat.
- Floor-attr values 1/8/12/15 (suspected one-way / stairs / encounter-related) — **unmapped** (open since 2026-06-08).

The toolkit bakes: per-cell pass grid into `.ffmap` (FFM1+, built from **layer 0 tiles only** — matches the verified original behavior that collision derives from the ground layer), `data/chipanim.bin` (FCAN), `data/chipfloor.bin` (FCFL).

## Engine behavior

`Field::isSolid(c,r)`:
1. Out of bounds → solid. 2. No-clip → walkable (bounds still block). 3. A standing chara event (`img>0`, boot 0/1, appear-passes) on the cell → solid; position triggers stay walkable so you can step onto doors. 4. `pass[cell] & 0x0F == 0` → solid.

**MEDIUM divergence:** the engine collapses the 4-direction mask to walkable/solid (any nonzero nibble = walkable). The original `CheckMovePass` tests the *specific direction bit* and also handles map-edge **wrap** via per-map wrap flags (world maps wrap, dungeons clamp — roadmap §4.2). FFSmith always clamps; one-way passages would not work. Revisit when a wrapping world map becomes walkable content.

Damage floors (`Host::checkFieldHazard`): on arrival at a *new* cell in `damageCells_` (walkable cells whose chip has floor bit 0x10 — solid lava-wall chips excluded by the walkability test in `buildAnimCells`), each living member loses `max(1, maxHP/16)`. **MEDIUM:** 1/16 is an approximation; the original per-step amount is not decoded.

`buildAnimCells` resolves each cell's *topmost non-zero* chip across layers to find its (mc, inner) for both anim and floor lookups — matches `GetUpdateChipIDOfPosition`'s of-position semantics (HIGH for anim; the original may evaluate floor attrs on a specific layer — unverified, MEDIUM).
