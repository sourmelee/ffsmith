# FFSmith — Runtime Pipeline

*Audit snapshot 2026-06-10. Source: `src/host/host.cpp`, `src/main.cpp`, `src/field/field.cpp`.*

## Boot sequence (`main.cpp`)

1. Parse CLI; resolve bundle dir (default: exe folder via `SDL_GetBasePath`).
2. `list_maps` / `list_sprites` scan the bundle.
3. Load the initial map (`load_ffmap`) → compose **ground** (`compose_range(0..threshold]`, opaque) and **overhead** (`(threshold..n)`, transparent) framebuffers.
4. Headless early-exits if requested: `--shot`, `--events`, `--walk`, `--vmtest`.
5. Construct `Host`; load text bank (`bankOf(mapKey)` = map group), menu data (`loadMenuData`: items/chars/monsters/spells/levels/spritegeo + audio init + chip tables), common events, start table (`data/start.bin` → New Game map), title + intro assets, save presence.
6. Self-test flags run and exit here if given.
7. Default boot mode: **Title** (`--debug` → Debug launcher).

## Frame loop

`Host::run` → `while(frame())`. `Host::frame`:

- `pumpEvents` — SDL events → `raw_held_` bitmask; hotkeys (F1 launcher, F2 noclip, F3 overlay, F4 HUD, F5/F9 save/load, B test battle, ±zoom, Esc).
- Fixed timestep: accumulate real time, run `stepInput(); update(dt)` at `tick_hz` (default 60), clamped at 0.25 s; render once per frame. Headless mode (`max_frames ≥ 0`) runs one update+render per call.
- `stepInput` computes `pressed/released` edges from `held`.

`Host::update` dispatches on mode:

```
++animTimer_; updateAudio();              // BGM follows mode/map every tick
Debug  -> updateDebug   (launcher cursor)
Battle -> updateBattle  (phase machine, below)
Title  -> 3-item menu; Confirm: NewGame->Intro | Continue->loadReq | Debug
Intro  -> 3 beats on Confirm; beat 3 -> debugSelectMap(startMap) + dbgStart_
else   -> menuOpen_ ? updateMenu : { BTN_MENU opens menu; field_->update + checkFieldHazard }
```

**HIGH:** Menu and Dialog are *overlays*, not modes — matching the decoded `GameClass::MainFunc` model (Boot/Title/Field/Battle as modes, Menu/Dialog drawn over a paused field).

## Main-loop orchestration (back in `main.cpp`)

After each `host.frame()` the outer loop consumes requests:

- `consumeSaveRequest` → `writeSave` (FSAV v5: map key, pos, facing, sprite, party, inventory, gil, script-state blob).
- `consumeLoadRequest` → `readSave` + `loadInto` + restore party/inventory/gil/script state.
- `consumeDebugStart` → load launcher-selected map, pick player sprite (lead's CHPK from `chars.bin`, fallback first map NPC), Mode::Field.
- `field->consumeWarp()` → `find_map_key(mapId)` → `loadInto` (recompose, re-wire Host, reload text bank, `field->enterMap()` fires on-load autos).

`Warp` is held while dialogue/choice is active (`Field::consumeWarp` returns empty until text is read) — this is how warp-after-dialogue sequencing works.

## Field tick (`Field::update`)

1. `pumpAuto()` — run one queued auto event when idle (no dialogue/choice/warp pending). Loop guards: ≤4 runs per event, ≤32 per map visit.
2. Choice menu eats input (up/down/confirm/cancel).
3. Confirm → advance dialogue / pick choice / talk to faced NPC / boot-8 confirm-in-rect.
4. Movement: tile-stepped, `prog_ += speed_(2)` px/tick toward the target cell; on arrival, step triggers (boot 2/3/6/7 rect) fire.
5. New input only when not moving: face, collision test (`isSolid`: bounds → noclip → standing-NPC block → pass nibble), commit move.

## Render (`Host::render`, Field mode)

Order: ground map texture (camera-cropped src rect) → animated-cell overdraw (current frame per `chipanim`) → NPC sprites (appear-gated; faint green markers for invisible triggers) → player sprite (walk cycle `animCol`: idle/A/idle/B by quarter-progress) → **overhead texture** (player walks behind canopies) → collision overlay (F3) → HUD (F4) → dialogue/choice box → menu overlay. Integer zoom via `SDL_RenderSetScale`; camera centers on the player's interpolated pixel position, clamped to map bounds, small maps centered.

**MEDIUM:** This is a *visual* equivalent of `DrawMapAll`'s depth sort (chips 0x2000000 vs chars 0x1000000 keys). The original Y-sorts characters *among themselves* within the ground/overhead split; FFSmith draws NPCs in event order, player last — multi-NPC overlap order can differ from the original.

## Battle phase machine (`updateBattle`)

Phases: `0` command (Attack/Magic/Defend/Run) → `5` target select / `6` spell select → `1` resolve player action → `2` resolve enemy action → `3` win (rewards) / `4` lose. `beginNextTurn` runs the ATB pick (`pickNextActor`: gauges += SPD until someone crosses 256). Victory → `awardBattleRewards` (EXP/gil/level-ups into persistent party) → `endBattle` writes HP/MP back and returns to Field. Battle BGM set in `startBattle`; field BGM self-corrects from the per-tick `updateAudio` poll.

## Audio pipeline

`updateAudio` (every tick): Field → `map()->field_bgm`; Title/Intro → `titleBgm_` (placeholder 18); Debug → stop. `AudioManager::playBgm` is a no-op when the id is already playing, loads `audio/snd0_{id}.wav` (IMA-ADPCM, decoded by `SDL_LoadWAV`, converted via `SDL_AudioCVT`), loops per the `data/audio.bin` (`FAUD`) flag table. SFX (`playSe`) cache `audio/snd2_{id}.wav`, ≤16 simultaneous voices, mixed in one callback. SE 1 = "decide" (the only SE wired so far).
