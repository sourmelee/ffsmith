# System: UI (Title, Intro, Field Menu, Battle UI, Debug Launcher)

*Snapshot 2026-06-10. Source: `src/host/host.cpp`. These are functional equivalents, not ports of `MenuClass` (278 methods, unread).*

## Title (`Mode::Title`)

`ui/title.tex` logo (currently the **FFL** logo — the FFD-branded asset isn't baked yet) over a 3-item menu: New Game (→ `newGame()` + Intro), Continue (greyed without `save.dat`; → load request), Debug Menu (→ launcher). Decoded context: original New Game = `InitGameData` + `ReadStartData`; `e3_param.dat` is the E3 demo start, **not** retail — retail start comes from boot_data §1 scenario record 0 (`data/start.bin`).

## Intro (`Mode::Intro`)

Three Z-advanced beats: prologue text crawl (real "In an age long past…" string, extracted by content into `data/intro.bin` FINT), FF logo card, "Prologue" chapter card. Then loads the start map via the debug-start path with the lead's CHPK sprite (Sol = fldchr13). MEDIUM fidelity: pacing/fades are placeholders.

## Field menu (overlay)

Root list Item/Equip/Status/Save/Quit (`kMenuItems`, host.cpp:14).
- **Item**: scrollable inventory with counts, gil, description pane; Z = Use (`useItem`: desc-text heuristics — digits = heal amount, "MP" targeting, "Knocked Out" = revive 25%; most-wounded living target).
- **Equip**: 6 slots (Weapon/Off-hand/Head/Body/Arms/Acc. — **FF-convention labels**, the start data only fills weapon slots); candidates filtered by baked `item_type` (1–15 weapon, 16 shield, 17–19 head, 20–22 body, 23 hands/acc; `equip_type` is always 0 in the data — item_type is the real discriminator); swap returns old gear to bag.
- **Status**: level, live HP/MP vs growth-table max, STR/SPD/VIT/INT/MND, weapon.
- Missing: Magic/ability pages, formation, config, real Save slot UI (Save writes immediately).

## Battle UI (`Mode::Battle`)

Battle background (`ui/btlbg.tex`), enemy name boxes + HP bars (target highlight), party rows (HP/MP, KO grey), right-hand command window by phase (commands / spell list / target / message). Layout is original-inspired, not pixel-matched (LOW fidelity by design at this stage).

## Debug launcher (`Mode::Debug`, default with `--debug`)

10-row cursor menu: map (with L/R jump-10), character sprite, spawn X/Y, facing, no-clip, collision overlay, HUD, scale, START. Field hotkeys: F1 launcher, F2 noclip, F3 overlay, F4 HUD, F5/F9 save/load, B test battle, ±zoom. This is pure tooling — no original counterpart; keep it isolated from gameplay code when refactoring.

## Text rendering

All UI text via the single baked bitmap font (see dialogue.md). No proportional font, no icons.
