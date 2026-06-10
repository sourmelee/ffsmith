# System: Camera

*Snapshot 2026-06-10. Source: `src/host/host.cpp:render()` (Field branch). Original: `FieldClass::MoveScroll` / `InitScroll` (c:130788), `GetCurrentScrollX/Y`.*

The camera is **computed per frame inside `Host::render`** — there is no camera object.

- Viewport = window size / integer scale (min 16×16); zoom via `SDL_RenderSetScale`.
- Target: center the player's interpolated pixel position (`pixelX/Y + tile/2`).
- Clamped to map bounds; maps smaller than the viewport are centered (offset, black borders).
- Because it follows `pixelX/pixelY + prog_`, scrolling is pixel-smooth without extra logic — confirmed equivalent in feel to the original's float-scroll during the 2026-06-08 field-polish pass ("camera confirmed already pixel-smooth — no change needed").

## Divergences / open

- **No scripted camera.** The original supports script-driven scroll (`InitScroll` targets, `ScrollToOffsetCellX/Y`, `IsAddScroll`); cutscenes that pan the camera will look wrong until ported. MEDIUM priority once cutscene fidelity matters.
- **No wrap-aware scrolling** (world maps): clamping only, consistent with movement's no-wrap limitation.
- **No screen shake / battle transitions.**

Confidence: HIGH that this matches FFSmith's needs today; LOW as a complete model of original `MoveScroll` (its smoothing/lead behavior was never fully read — the M2 milestone accepted "follows and clamps" as done).
