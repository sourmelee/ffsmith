# Repository Relationships (Engine view)

*Audit snapshot 2026-06-10. The toolkit-side counterpart (with the full baked-format catalogue) is `../../Python/docs/architecture/asset_pipeline.md` and `.../architecture/repository_relationships.md`.*

```
Workspace root (D:\FFD\cowork\Java Analysis Final Fantasy Legemensions)
├── Engine/    FFSmith (this repo; own .git, 34 commits since 2026-06-01)
├── Python/    FFL/FFD Toolkit (own .git = github sourmelee/ffl-ffd-analysis, repo root = Python/)
├── Decomp/    Ghidra exports — ground truth (NOT in either git repo)
│   ├── FFV_FFD_compare/libjniproxy.so_new.{c,h}   FFD Android native (~254K lines)
│   ├── FFV_FFD_compare/libff5lib.so.{c,h}         FFV — "Rosetta Stone" for shared Mtx* engine
│   └── Functions/                                  pre-cut single-function extracts
├── Android/   FFD APK + main.obb + extracted assets (proper_obb/ = bake source)
├── Mobile/    FFL .jar/.jam + 6 dumped .sp scratchpads + decompiled Java
├── CLAUDE.md, HANDOFF_NEXT_CHAT.md, PROJECT_KNOWLEDGE_EXPORT.md   workspace-level docs
└── memory (Claude-side ffd_*.md notes)             institutional knowledge index
```

## The symbiosis contract (HIGH — enforced by convention since M1)

1. **Toolkit decodes; engine consumes.** Every on-disk format is parsed exactly once, in Python. FFSmith reads only the baked bundle (`maps/*.ffmap`, `tex/*.tex`, `sprites/*.tex`, `text/*`, `ui/*`, `audio/*`, `data/*.bin`, `manifest.json`).
2. **Toolkit defines correctness.** A C++ loader must byte/pixel-match the Python parser before it is trusted (M1 gate: byte-identical map renders).
3. **Versioned bundle.** The `.ffmap` magic carries the version (`FFM0`→`FFM4`); the engine loader is backward-compatible down the chain (`bundle.cpp:load_ffmap` checks `buf[3]`). **Bumping FFM requires rebuilding the engine before rebaking** (bit Jack once at FFM3 — CHANGELOG 0.7.24 warning).
4. **Knowledge flows both ways.** Offsets/formulas RE'd while building FFSmith land in toolkit parsers (e.g. spawn header → FFM4, appear blocks → FFM3, BGM ids → reserved u32) and in `PROJECT_KNOWLEDGE_EXPORT.md` + memory notes.

## Who reads what

| Artifact | Producer | Consumer(s) |
|---|---|---|
| `main.obb` / `proper_obb/` | Square Enix / Colmines92's tool | Toolkit only |
| baked bundle | `python ffd_toolkit.py --bake-ffsmith OUT --proper ../Android/proper_obb` | FFSmith |
| `save.dat` (FSAV v5) | FFSmith | FFSmith (+ an independent Python parser was used once to verify round-trip) |
| `sprite_grid.json` | Toolkit Animation tab (manual annotation) | Toolkit baker (`_bake_sprite_geo`) |
| `libjniproxy.so_new.c` line citations | Ghidra | Both repos' docstrings/comments |

## Git hygiene notes

- The two repos version independently; engine-only changes do **not** bump the toolkit version (established practice, e.g. FSAV v2/v3 work).
- Clean-room rule: no original assets in either repo; baked output is gitignored; users bake from their own OBB.
- `Decomp/`, `Android/`, `Mobile/` are unversioned local material — treat paths into them as machine-specific.
