# Engine Technical Debt

*Audit 2026-06-10. Ordered by risk × cost-of-delay.*

1. **`Host` god object** (host.cpp ≈ 1,565 lines / 81 KB): window, loop, input, audio policy, mode machine, title, intro, field render, full menu system, full battle system, persistent game state, debug launcher, self-tests. Every new feature lands here. Risk: merge churn, accidental coupling (battle reads menu cursors' neighbors), untestable units. See refactoring_candidates.md.
2. **Raw-pointer ownership triangle**: `main.cpp` owns `FfMap`/`Field`; `Host` keeps `Field*`; `Field` keeps `const FfMap*`. Correct today only because `loadInto` re-wires everything in one place. A second call-site or an exception path dangles pointers. Cheap fix: move map+field ownership into Host.
3. **Save/load logic split across `main.cpp` (file I/O) and Host (state accessors)** with field position threaded through function args. The original puts this in GameClass; FFSmith should grow a `game/save.{h,cpp}`.
4. **Global `std::rand()`** seeded with `time(nullptr)` — non-reproducible runs, and the original PRNG is unidentified (roadmap risk). Headless traces that involve randomness aren't deterministic (battlesim, RandomJump, encounter groups). Introduce a seeded RNG service now; swap in the original algorithm when decoded.
5. **Magic numbers as gameplay**: hardcoded starting inventory/gil (`newGame`), Phoenix Down item id 426 / Potion 420, `titleBgm_=18`, hero CHPK fallback 13, ATB threshold 256, damage-floor /16, anim speed ×8, spell-knowledge INT/MND≥2. All flagged in comments, but they should live in one `approximations.h` (or be baked) so the "not yet decoded" surface is greppable.
6. **No automated test runner**: self-tests exist but require a baked bundle and a human to invoke each flag (testing_strategy.md).
7. **Heuristic item effects** (description-text parsing) will mistranslate non-English bundles and items whose descriptions don't carry numbers. Real fix: decode the item-effect fields of the 54-byte item body.
8. **Text pipeline is ASCII-only** (95-glyph atlas). Blocks JP/FR/ZH/KR display despite the data being fully multi-language. Will eventually force a font/glyph rework touching every drawText caller — the longer UI code accretes, the costlier.
9. **`compose_range` recomposes whole maps on every warp** (CPU, full-map RGBA in memory). Fine at FFD map sizes; the world map (256×256 cells × 32 px = 8192² ≈ 268 MB RGBA) likely breaks this. Measure before the world-map milestone; may need tile-batch rendering instead of a single texture.
10. **Windows build config drift risk**: CMake globs `src/*.cpp` (`CONFIGURE_DEPENDS`) — convenient, but stale build dirs have bitten before; `.vs/`, `build/`, `out/` all exist in the tree with different states.
