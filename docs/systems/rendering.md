# System: Rendering

*Snapshot 2026-06-10. Source: `src/render/compositor.cpp`, `src/host/host.cpp` (render paths).*

## Two-stage model

1. **Offline (per map load), CPU:** `compose_range(bundle, map, lo, hi, opaque)` builds an RGBA framebuffer from tile layers. Two calls per map: ground = layers `[0, threshold]` over opaque black; overhead = layers `(threshold, n)` over transparency. Uploaded once as static `SDL_Texture`s.
2. **Per frame, GPU (SDL_Renderer):** camera-cropped blit of ground → dynamic elements → overhead → UI.

## Compositor spec (HIGH — byte-identical to toolkit `ExtractTab._render_android_map`)

- Tile word u16: low byte = tile index within sheet, high byte = slot selector (0 → `mc_slot0`, 1 → `mc_slot1`). Word `0x0000` = skip.
- Slot sheet load: `tex/mc{N}_{V}.tex`, falling back to variant 0 (`load_slot`).
- Tile size: 32 if first renderable sheet width ≥ 512 else 16; sheet columns = `sheet_w / TS`.
- Alpha compositing reproduces **PIL's `div255` rounding** (`(tmp>>8 + tmp)>>8`, tmp = a+0x80) including the opaque-dst fast path — this is what makes engine output bit-equal to Pillow's `alpha_composite`. Do not "simplify" this math.

## Per-frame field elements (host.cpp `render()`)

Order matters: ground → **animated tiles** (overdraw current frame of each `chipanim` cell; frame = `animFrameOf(timer, type, frames, speed)`, ping-pong for type 1, `ticks/frame = speed·8` — *MEDIUM, exact speed table not decoded*) → **NPC sprites** (appear-gated) → **player** → **overhead** → debug overlays → dialogue/menu.

Sprite drawing (`drawSprite`): object sprites use baked `spritegeo` frame+anchor (`dst = (lx+tile/2+px, ly+tile+py)`); character sheets ≥149×149 use the 48×48 grid (origin (1,1), pitch 50; rows = facing Down/Up/Left, Right = Left h-flipped; cols = idle/walkA/walkB); small sheets fall back to top-left ≤48×48 cell, feet-aligned.

## Other modes

Title/Intro/Battle/Debug each clear and draw directly (no map texture). Battle: `ui/btlbg.tex` scene + enemy boxes/HP bars + party rows + command window. Text everywhere is the baked bitmap font (`text/font.tex` + `FMET` metrics, DejaVuSansMono atlas, 95 glyphs from `first=32`; non-ASCII renders `?`).

## Known divergences from the original (documented, intentional for now)

- No palette engine: tilesheets are pre-rendered RGBA PNGs from the OBB; `PaletteCtrlClass` (fades, flashes, palette anims) unimplemented — script fade ops are log-skipped.
- No perspective/tilted world-map renderer, no Far background layer (`LoadFar` params are parsed past at bake, not used).
- NPC overlap order is event order, not the original's per-row depth sort (see runtime_pipeline.md, MEDIUM).
- Default logical size 256×176 (`HostConfig`) is a working choice, not a confirmed original resolution (LOW).
