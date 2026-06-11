# System: Camera

*Snapshot 2026-06-10. Source: `src/host/host.cpp:render()` (Field branch). Original: `FieldClass::MoveScroll` / `InitScroll` (c:130788), `GetCurrentScrollX/Y`.*

The camera is **computed per frame inside `Host::render`** with a persistent eased position (2026-06-11): the target is the player or, after `0x1b CameraFollow`, the look-actor (`Field::lookTargetPixel`); `camPX_/camPY_` glide toward it (step = max(2, dist/12)), snapping on map swaps. This reproduces the original's SetLookChara re-target + scroll glide (MEDIUM: easing curve is ours, not MoveScroll's decoded math).

- Viewport = window size / integer scale (min 16×16); zoom via `SDL_RenderSetScale`.
- Target: center the player's interpolated pixel position (`pixelX/Y + tile/2`).
- Clamped to map bounds; maps smaller than the viewport are centered (offset, black borders).
- Because it follows `pixelX/pixelY + prog_`, scrolling is pixel-smooth without extra logic — confirmed equivalent in feel to the original's float-scroll during the 2026-06-08 field-polish pass ("camera confirmed already pixel-smooth — no change needed").

## Divergences / open

- ~~No scripted camera~~ **0x1b CameraFollow implemented 2026-06-11** (re-target + ease). Still open: explicit scroll-offset ops (`ScrollToOffsetCellX/Y`), exact MoveScroll easing curve.
- **No wrap-aware scrolling** (world maps): clamping only, consistent with movement's no-wrap limitation.
- **No screen shake / battle transitions.**

Confidence: HIGH that this matches FFSmith's needs today; LOW as a complete model of original `MoveScroll` (its smoothing/lead behavior was never fully read — the M2 milestone accepted "follows and clamps" as done).
