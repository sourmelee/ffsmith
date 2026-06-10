# Engine Testing Strategy

*Audit 2026-06-10.*

## What exists (and works)

- **`--vmtest`** — 11 assertions on the event VM + script state + save blob, **no SDL or bundle needed**. The only true unit test in the project. Exit code reflects pass/fail.
- **Bundle-dependent self-tests** (need a baked bundle + `SDL_VIDEODRIVER=dummy`): `--battlesim N`, `--itemtest`, `--equiptest`, `--leveltest`, `--revivetest`, `--menutest`, `--dmgtest`, `--walk <script>`, `--events`.
- **Pixel gates**: `--shot`/`--fieldshot` dumps diffed against toolkit renders (M1's byte-identical proof; reused for sprites/z-order/intro screenshots).
- **Human verification**: GUI feel, audio, Windows build — Jack.

## Gaps

1. Nothing runs automatically; "all green" claims decay (contradiction report #8).
2. Randomness makes `--battlesim` non-reproducible (no seed flag).
3. No golden input trace vs the original game (roadmap §4.4 step 6 — never executed; movement speed and formulas are unconfirmed against the real runtime).
4. VM tests use synthetic bytecode; only a few real-data cases (m501 doors) are encoded as assertions.
5. Loader robustness untested against truncated/corrupt bundles (loaders bounds-check but nothing exercises them).

## Recommended priorities

1. **A `run_selftests` script** (shell/py) that builds, runs `--vmtest` plus every bundle test against a checked-path bundle, and diffs `--shot g0_p0_m101` against a stored toolkit render hash. One command = current truth. (Small; highest value.)
2. **`--seed N`** for deterministic sims (pairs with the RNG service refactor).
3. **Real-data VM fixtures**: extract 3–5 event scripts (door warp, choice, appear-gated NPC, the m0 auto chain) as hex into `--vmtest`-style assertions so VM regressions surface without a bundle.
4. **Golden trace harness** (when emulator access exists): scripted input → per-frame position log on both engines; the roadmap already specifies this. Until then, mark movement timing LOW confidence.
5. **Battle exact-match table**: once `SetJobStatus` is decoded, a fixture of (attacker stats, defender stats, seed) → damage from the original formula, asserted in C++.
