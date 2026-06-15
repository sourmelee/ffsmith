# FFSmith — Engine State Report

*Audit snapshot 2026-06-10 (commit `d2aacc2`, 34 commits since 2026-06-01); data-table completion (real spell/item/job tables) added 2026-06-13; N2 job-stat derivation added 2026-06-14. This is the authoritative "where is the engine actually at" document. Confidence: HIGH = verified in code + self-test; MEDIUM = implemented but approximated or unverified against the original; LOW = guess/placeholder.*

## Ground-truth references (2026-06-14)

Shared engine systems are now disambiguated against **two** decompiles. **Primary: `libjniproxy.so` (FFD Android)** -- `Decomp/FFV_FFD_compare/libjniproxy.so_new.c`, citations use its c-file line numbers. **Sanctioned 2nd ground-truth: the FF5-PC decompile** -- `Decomp/FFV_FFD_compare/FFV_Game.exe.unpacked.exe.c` (ImageBase 0x400000, so **decomp addr = RVA + 0x400000**; field map in `Decomp/FFV_FFD_compare/ffd_port/docs/ffv_pc_addresses.md`). The same-engine premise is proven by the `ffd_port` re-bake (FF5's unmodified loaders parse re-baked FFD boot_data). Use FF5-PC only to clarify murky Android code; **never let it override `libjniproxy`** -- the two engines share structure but differ in model (e.g. FF5 job stats are flat bonuses, FFD's are %-growth).

## Maturity summary

FFSmith is **~10 days old and already plays a vertical slice of the real game**: title menu → New Game → intro cinematics → the retail opening cutscene chain (m0 → m101 → m100 → m101 → m200 → m1 → m300, ending at the scripted prologue battle) → walkable fields with real tiles/collision/sprites/animation/z-order → menus (Item/Equip/Status with real data and working actions) → turn-based ATB battles with real stats, rewards, level-ups → save/load. Since the 2026-06-10 battle-RE pass, **scripted battles work end-to-end** (`0x50` → real formation from `encounters.bin` → script resume, `--enctest` PASS) with real decoded monster stats (FMN2), and decoded random-encounter areas ship behind `--encounters` (approx roll). Still missing for story-playability: `bsc.dat` battle scripts, AP/abilities, shops, NPC movement, and the perspective world-map renderer.

## Milestone ledger

| Milestone | Status | Evidence |
|---|---|---|
| M0 host + loop | ✅ HIGH | `Host::frame` fixed-timestep; `--frames` headless |
| M1 static map render | ✅ HIGH | byte-identical vs toolkit (g0_p0_m101, m501); `compositor.cpp` reproduces PIL div255 |
| M2 movement + camera + collision | ✅ HIGH | `Field::update`, `isSolid` (capk pass nibble), clamped follow camera; `--walk` traces |
| M3 event VM, NPCs, dialogue, triggers, warps | ✅ HIGH | `event_vm.cpp`, FFM2+ events, msg banks; verified m500↔m501 round trip, 4507/4514 warp records in-bounds |
| M4 game state machine | ✅ HIGH | `Host::Mode` {Debug, Title, Field, Battle, Intro}; Menu/Dialog as overlays — mirrors `GameClass::MainFunc` shape |
| M5 menu pages | ◑ MEDIUM | Item/Equip/Status real data + item-use + equip-swap; **no** Magic/Config/Formation/Shop pages |
| M6 battle | ◑ MEDIUM | ATB loop, groups 1–3, target select, Magic, decoded physical formula core, EXP/gil/level-up/revive; large approximation tail (below) |
| M7 save/load | ✅ HIGH (own format) | `FSAV` v5 in `main.cpp`; round-trips verified incl. ~4 KB script-state blob. **Not** the original `save.bin` format |
| M8 audio | ✅ MEDIUM | per-map field/battle BGM, title BGM, "decide" SE; `titleBgm_ = 18` is an unconfirmed placeholder (host.h:149) |
| Post-M8 Event VM v2 | ✅ HIGH | branching (`0x3d` if-not-goto), flags/vars banks, appear conditions, choices (`0x3c`), CallEvent (`0x66`), auto events, common pool — `--vmtest` 11/11 |
| Post-M8 Scripted battles (N1) | ✅ HIGH | `0x50` decoded + implemented: formation tables (FENC), real monster stats (FMN2), VM pause → battle → resume, result via GetReference t8 — `--enctest` PASS |

## What is real vs approximated

### Decoded-and-faithful (HIGH)
- Map composition, tile-word slot dispatch, zero-skip, TS 16/32 heuristic (`compositor.cpp`).
- Collision pass nibble from `capk.dat` word A & 0xF; damage-floor bit 0x10 of `(A>>18)&0x3f`; tile-animation bits 8–17 (frames/speed/type).
- Overhead z-order split at the baked per-map threshold (`FieldClass+0xdc2c`).
- Event-script execution model: block registry semantics, BE operands, var-indirection masks, `ScriptIf` jump-on-fail, `0x41` map/layer/x/y/dir, `0x6b` BulkSetVars, `0x66 CallEvent` + common event 0x104 door idiom, `CheckEventAppear` 6-slot gate (`script_state.cpp:183`).
- Flag/var bank layout (sizes, paging, type-5 system specials) from `SetReferenceFlag`/`GetVariable` (script_state.h header comment cites the c-file lines).
- Physical damage core: `(W<<6·H>>5 + spread − D<<6) × (3A+L) / 1024` per `docs/BTLACT_MAP.md` (`Host::physDamage`).
- Monster EXP/gil offsets (body[6]/body[10] BE u32), §8 EXP thresholds + HP/MP growth (`levels.bin`).
- New Game start: boot_data §1 scenario record 0 → map 0, map-default spawn (FFM4 header).
- field_anm character grid (48×48, pitch 50, rows=facing, Right=Left-flipped) and per-sprite object geometry (`spritegeo.bin`).
- **Message vs narration split (2026-06-14):** op `0x00` SetMessage = windowed dialogue box; op `0x01` ScriptSentence = the FieldClass *Sentence* system (libjniproxy `InitSentence`/`DrawSentence` -- accumulating full-screen telop lines over the scene, no window). The engine now renders `0x01` as a full-screen blue narration overlay (was incorrectly shown in the message box). `Field::inSentence`/`sentenceLines`.
- **UI polish (2026-06-14):** text drop-shadow + thousands separators; coloured HP/MP gauges (`drawGauge`) in party panel/Status/battle; Config settings list (aspect/opacity/window colour/shadow/message speed, + debug); dialogue typewriter; battle floating damage numbers + skinned command window. Battle sprites still TODO (needs monster-sprite bake).
- **FFD dual party + menu (2026-06-14):** two parties (Light 0 / Dark 1, up to 5
each) per libjniproxy `GetPartyMemberID` (side @+0x21424, id arrays @+0x21428,
roster mask @+0x2172c); `parties_[2]`/`partySide_`/`switchSide`, FSAV v6. Main menu
is the full FFD set (Magic/Job stubbed), with a Formation tab + active-party panel.
- **FFD window skin (2026-06-14):** `Host::drawWindow` reproduces `GameClass::DrawWindow` (libjniproxy 153133-153153) -- the 3-stop blue vertical gradient + frame -- on menus, pages and the message box (was flat rects).
- **Warp fade recovery (2026-06-14):** a door/map warp that followed a fade-out now ramps the fade back in (the script's fade-in half is lost because warps run outside the VM), guarded by `fadeGen()` so genuine cutscene blacks are kept.

### Approximated / heuristic (MEDIUM — flagged in code comments)
- **ATB**: gauge += SPD per step, threshold 256, random initial stagger (`pickNextActor`). Original turn scheduler not decoded.
- **Magic**: the spell *table* is now **real decoded data** (0.7.27 — 251 spells with MP=body[7], power=body[19], type/element from the magic body; `spells.bin` FSPL). Still approximated: spell *knowledge* gate = `INT≥2`/`MND≥2` (no learned-ability slots yet) and the damage *application* `power + INT·3 − def/4 ± rand(4)` — NOT the decoded `CalcMagicDmg` formula. Status/buff spells aren't baked (no status system).
- **Damage formula tail**: crit/element/race/status/hit-count/back-attack/defense% modifiers absent. (Resolved 2026-06-14: the job-derived attack stat `A` and the other four attributes now come from `SetJobStatus` -- `memberStat()` = `base_stat[level]*jobPct/100`, FLVL base[] + FJOB; raw-STR `A` retired. Equip/ability stat bonuses still omitted; enemy `W` = monster body[15].)
- **Encounters**: scripted battles (0x50) now use the real formation tables; the debug **B** key still uses the old difficulty-band picker. Random-encounter areas are real data; the per-step roll is approximated (`--encounters`, default off; rate-sum/256 per new cell). Enemy ATB speed = level (decoded: BTLACT+0x40 = level for enemies) — the *player-side* turn scheduler is still the FFSmith approximation.
- **Run** = 50% coin flip; **Defend** = damage/2.
- **Item effects**: equip **ATK/DEF are now real decoded data** (0.7.27 — body[32], keyed by item_type; `items.bin` FITM). Consumable *use* effects are still parsed from description text (digits → heal amount; "Knocked Out" substring → revive) — the heal *magnitude* isn't a literal body field; the real item-effect table is undecoded.
- **Damage floors**: drain `maxHP/16` per step — exact original amount not decoded (host.cpp:169 comment).
- **Tile-anim speed**: `ticks/frame = speed·8` — exact speed table not decoded (host.cpp:152).
- **EXP split**: full EXP to every survivor (original may divide); reward rate fixed at 100%.
- **Starting inventory/gil**: hardcoded `{Potion×5, Phoenix Down×2, …}, 500 gil` in `newGame()` — not from game data.
- **Level-ups**: HP/MP grow via the §8 curve (job-scaled) on level-up. **The five attributes (STR/SPD/VIT/INT/MND) are now job-derived** (FFSmith 0.1.1 / toolkit 0.7.28 — `memberStat()` = §8 per-level base-stat × FJOB per-job stat% / 100) and recompute from the member's current level whenever the battle party is built, so they rise with level. Still approximated: equipment/ability stat bonuses are omitted, and level-up persists no per-attribute deltas (attributes are derived on the fly, not stored).
- **Choice menu text**: option *value* treated as a message id (event_vm comment "0x3c choice menu: each option value is a message id") — provisional; real choice-line source still open.
- **Boot conditions 2/3**: treated as step triggers "pending their trigger-type RE" (field.cpp:29).

### Missing entirely
- ~~`0x50 ScriptEncount`~~ **DONE 2026-06-10**: scripted battles pause the VM, fight the real formation (encounters.bin, FENC), resume the script with the result readable via GetReference target 8 (`--enctest` PASS). Still missing within battles: `bsc.dat` battle scripts, formation x/y placement rendering, per-formation loss handling (scripted losses currently revive-to-1HP and resume — flagged approximation).
- Random encounters: decoded areas ship in FFM5 and a flag-gated approximation roll exists (`--encounters`, default off) — the original roll formula is still open.
- ~~NPC scripted movement~~ **DONE 2026-06-11**: full cutscene direction — actor system (per-event movable actors with walk-anim/facing/alpha), 0x68 command sequences (decoded 68-command table), 0x69/0x32 script suspension with call-stack resume, 0x1b camera re-targeting with eased pans, 0x2a screen fades, 0x20/0x21/0x55 teleports/visibility. Retail intro plays headless with direction through the prologue battle into the m150 exit (`--cuttest` PASS). Remaining in this area: poses, jump arcs, `MoveCharaAuto` idle wander (townsfolk still stand still).
- AP/ability learning, status effects, elemental/crit modifiers, shops, NameInput UI, palette ops.
- Common parallels (0x102/0x105/0x106…), page register (0xe474) full semantics, op 0x32 message-wait.
- Perspective/tilted world-map renderer + Far background (m1 flyover renders flat); FFD title-logo asset (bundle ships the FFL logo).
- Original save format (`GameClass::SaveGameData` → `save.bin`, 15 KB/slot) — FFSmith uses its own `FSAV`.
- Battle SFX (data-driven per weapon/spell), victory jingle (`ReserveJINGLE`), Mobile MFi→MIDI audio.

## Open questions blocking the next milestones

1. ~~ScriptEncount semantics~~ **RESOLVED + implemented 2026-06-10** (formations/areas/result decoded; see Python/docs/formats/battles.md). New frontier within battles: `bsc.dat` battle-script VM and the original encounter roll.
2. **Real turn scheduler / RNG** — original PRNG unidentified (roadmap Part 6 risk; needed before battle exact-match).
3. ~~**Job stat derivation** (`SetJobStatus`)~~ **RESOLVED 2026-06-14**: decoded (libjniproxy 152644-152705, FF5-PC `FUN_00468490` cross-check) and implemented -- the five attributes derive from the §8 per-level base-stat byte x per-job stat% (FLVL base[] + FJOB), `--jobstattest`. Equip/ability stat bonuses remain undecoded.
4. **Timing model** — assumed 60 Hz frame-locked; never confirmed against the original (roadmap Part 6 risk, still open).
5. **Title BGM id** — placeholder 18, confirm by ear.
6. **Real opening world-map presentation** — needs the perspective renderer.

## Documentation drift found in this repo (see ../development/contradiction_report.md)

- `README.md` contains stale "Next:" markers (e.g. "Next: EXP/gil battle rewards" while a later bullet says rewards are done) and describes the save as FSAV v2/v3 in places; code writes **v5**.
- `ENGINE_RE_ROADMAP.md` Part 5 table says "M2 is the immediate objective" — superseded by the appended status log (M0–M8 done).
- `docs/ASSET_PIPELINE.md` bundle layout (JSON data files, FFM0 ffmap) is the original draft; actual bundle is FFM4 + binary `.bin` tables.
