# FFSmith Asset Pipeline — Toolkit ⇄ Engine contract

## Principle

FFSmith never re-solves raw on-disk formats. The Python toolkit (`Python/ffd/`) already parses the OBB, maps, tilesheets, palettes, animations, and `boot_data` records with verified correctness. The toolkit **bakes** that into a small, documented, engine-friendly bundle; FFSmith loads the bundle. The toolkit's parse output is the engine's source of truth *and* its golden test data.

**Clean-room:** the toolkit reads the **user's own** `main.obb`; no original assets ship with FFSmith. Baked output (`assets/baked/`) is gitignored.

## Symbiosis rules (Toolkit and Engine are one project)

1. **Knowledge flows both ways.** Any struct/offset/formula/format FFSmith recovers from the decompilation is documented (`PROJECT_KNOWLEDGE_EXPORT.md` + memory) and, where it parses data, lands as a toolkit parser/feature too.
2. **Toolkit defines "correct."** A C++ loader must match the Python parser's bytes/pixels exactly before it's trusted (byte/pixel diff against toolkit output).
3. **Shared, versioned bundle format.** Engine and toolkit agree on a bundle version; bump it together.
4. **Toolkit exporter first.** A new asset domain gets a toolkit baker before the engine consumes it.

## Bundle layout (v0 draft)

```
assets/baked/
  manifest.json            # version, source-obb hash, table of contents
  tilesheets/
    mc{N}_{V}.png          # copied verbatim (already PNG; paletted, nearest-scaled)
  maps/
    g{G}_p{P}_m{M}.ffmap   # one baked map per file (format below)
  data/
    items.json  jobs.json  monsters.json  abilities.json  magic.json
    text_{lang}.json
```

### `.ffmap` (baked map) — v0

A flat little-endian record built from `parse_android_map_engine` + the raw chunk:

```
magic      "FFM0"          4 bytes
width      u16             cells
height     u16             cells
flags      u16             bit0 = wrap_x, bit1 = wrap_y  (from map header)
mc_slot0   i16 + var0 u8   primary tileset id + variant   (-1 = none)
mc_slot1   i16 + var1 u8   secondary tileset id + variant
n_layers   u8
per layer:
  has_tile u8, flag_a u8, flag_b u8
  if has_tile:  width*height * u16   tile words (low byte = tile_num, high byte = slot 0/1)
  if flag_a:    width*height * u8    attribute layer A (passability etc.)
  if flag_b:    width*height * u8    attribute layer B
event_len  u32
event      [event_len]     raw event-region bytes
```

> The event region is passed through **raw** because the event-script VM (opcodes `0x00`–`0xab`, execution model) is already specified — FFSmith's interpreter consumes it directly. Tile-word semantics, the slot selector, wrap flags, and attribute layers are all already-decoded (see `PROJECT_KNOWLEDGE_EXPORT.md` §6–7 and §16).

## Toolkit side — status

| Piece | Toolkit support | To do |
|---|---|---|
| Maps → `.ffmap` | `parse_android_map_engine` yields all needed fields | thin writer for the flat record |
| Tilesheets | already PNG in OBB | copy into bundle |
| Item/Job/Monster/Ability/Magic | parsers exist (`ffd/items|jobs|monsters|abilities/`) | emit JSON |
| Exporter entry point | — | add `--bake-ffsmith OUTDIR` CLI / menu command at M1 |

## Engine side — status

- **M1** reads `manifest.json` + one `.ffmap` + its tilesheet and renders the map statically.
- Loader lives in `src/data/`; it must byte-match the toolkit (rule 2). The first verification harness diffs FFSmith's static render against the toolkit's `MapTab` render of the same map.
