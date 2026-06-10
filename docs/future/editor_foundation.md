# Editor Foundation Analysis

*Audit 2026-06-10. Question: what would it take to build map/event/game editors on this stack — and eventually support custom games, not just FFD?*

## Already editor-ready (HIGH)

- **The baked bundle is a de-facto project format.** Flat, versioned, little-endian, fully specified (`Python/docs/architecture/asset_pipeline.md`). An editor could read/write `.ffmap` + `data/*.bin` today without touching OBB decoding.
- **The toolkit's annotation pattern** (mc_overrides.json, sprite_grid.json, cpk_to_mc_overrides.json, custom_palettes.json) is exactly an editor's override model: machine guess + human correction + provenance flags (`user_confirmed`, `auto_from`, `auto_confidence`). Generalize it rather than replacing it.
- **The compositor contract** (byte-identical render in Python and C++) means an editor preview in either language is authoritative.
- **Headless engine modes** (`--shot`, `--fieldshot`, `--walk`, `--events`) give an editor instant "run this map" verification hooks.

## Requires abstraction first

| System | FFD-specific assumption to remove |
|---|---|
| Tilesets | mc id/variant naming (`mc{N}_{V}`), TS=16/32 from sheet width heuristic → explicit tile size in the map header |
| Sprites | fldchr ids + the hardcoded 48×48 character template; `spritegeo` covers objects only → make ALL sprite geometry data-driven (the planned "generalize non-48 character grids" work is the first step) |
| Events | opcode set is the FFD VM; fine for FFD editing, but a custom-game editor needs either this VM as *the* scripting target (document it as such — it's nearly fully specified) or a compiler layer |
| Text | bank = map group convention; ASCII-only font | 
| Battle | stats live in baked tables derived from boot_data layouts; an editor needs schema-first table definitions instead of offset-derived ones |
| Save | FSAV hardcodes party shape (6 equip slots, 4 members) |

## Should become canonical project formats

1. `.ffmap` (FFM4) — already close; add: explicit tile size, wrap flags, named layers, and stop packing three values into "reserved u32".
2. A `tables.schema` (JSON) describing items/chars/monsters/spells columns, so editors and bakers share one definition instead of paired struct code in Python and C++ (today every format change touches `ffsmith_bake.py` **and** `bundle.cpp` by hand — the #1 future bottleneck).
3. `sprite_geometry.json` superset of sprite_grid.json covering characters too.

## Likely bottlenecks

- **Dual-maintenance of bundle formats** (Python writer / C++ reader) — every editor feature doubles it. Consider generating both sides from the schema.
- **Whole-map texture composition** for large maps (technical_debt.md #9) — an editor scrubbing through 1,655 maps wants the toolkit's lazy render, not full recomposition.
- **Host monolith** — an embeddable engine view (editor "play" panel) needs the loop/render core separable from menus/battle (refactoring_candidates.md #1).
- **Provenance at scale** — once edits mix with decoded data, every record needs the override pattern's `user_confirmed` discipline or RE regressions become invisible.

## Recommended first editor

The **Animation tab object-override panel already is one** (live preview + JSON write + baker merge). The next-cheapest with outsized value: a **map event inspector/editor** over FFM4 (events are structured, the VM is specified, and `--events` provides instant validation). Map *tile* editing should wait for the schema work.
