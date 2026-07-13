#include "field/field.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace ffsmith {

static const int DX[4] = {0, 0, -1, 1};  // DOWN, UP, LEFT, RIGHT
static const int DY[4] = {1, -1, 0, 0};

// NPC movement timing (data/field_constant.bin; decoded defaults built in).
static FieldConstant g_fieldConst;
void Field::setFieldConstant(const FieldConstant& fc) { g_fieldConst = fc; }

Field::Field(const FfMap* map, int tile, int startCol, int startRow)
    : map_(map), tile_(tile > 0 ? tile : 32), col_(startCol), row_(startRow) {}

static int dirFromHeld(uint32_t h) {
    if (h & BTN_UP)    return FACE_UP;
    if (h & BTN_DOWN)  return FACE_DOWN;
    if (h & BTN_LEFT)  return FACE_LEFT;
    if (h & BTN_RIGHT) return FACE_RIGHT;
    return -1;
}

// Boot condition classifies an event's trigger (FieldClass::GetEventBootCondition):
//   0 = auto/parallel-on-load, 1 = action/talk, 2/3/6/7/8 = position (step/range)
//   triggers.  Doors and map-edge warps are position triggers and can be type 0
//   (town building doors) OR type 1 (interior exits) -- so we key off boot, not type.
static bool inRect(const Event& e, int c, int r) {
    return c >= e.x && c < e.x + e.w && r >= e.y && r < e.y + e.h;
}
// Step/range triggers (FieldClass::CheckRangeEvent / CheckStartEvent): boots
// 6 (range-in on step end), 7 (range-in always), 8 (confirm in range) use the
// rect; 2/3 are kept as step triggers pending their trigger-type RE.
static bool isStepTrigger(const Event& e) {
    return !e.scripts.empty() &&
           (e.boot == 2 || e.boot == 3 || e.boot == 6 || e.boot == 7);
}
static bool isParallel(const Event& e) {    // boot 4/5: run whenever appear passes
    return !e.scripts.empty() && (e.boot == 4 || e.boot == 5);
}
static bool isStandingChara(const Event& e) {  // talk/auto NPC: solid, talk on confirm
    return e.img > 0 && (e.boot == 0 || e.boot == 1);
}

// Appear gate (FieldClass::CheckEventAppear): all present header conditions
// (flags / variables / item / member / timer) must pass; no state = appear.
bool Field::appears(const Event& e) const {
    if (!script_ || e.appear.empty()) return true;
    static const VMEnv kNullEnv{};
    return check_event_appear(e.appear, *script_, env_ ? *env_ : kNullEnv);
}

bool Field::isSolid(int c, int r) const {
    if (c < 0 || r < 0 || c >= map_->w || r >= map_->h) return true;
    if (noClip_) return false;                 // debug no-clip: only bounds block
    // Standing NPCs/objects block movement at their CURRENT (actor) position;
    // position triggers (incl. visible door sprites) stay walkable.
    for (const auto& a : actors_) {
        if (a.evIndex < 0 || a.evIndex >= (int)map_->events.size()) continue;
        const Event& e = map_->events[a.evIndex];
        if (!isStandingChara(e) || !a.visible || !appears(e)) continue;
        if ((a.col == c && a.row == r) || (a.moving && a.tcol == c && a.trow == r)) return true;
    }
    if (map_->pass.empty()) return false;
    uint8_t nib = map_->pass[(size_t)r * map_->w + c];
    return (nib & 0x0f) == 0;
}

// An interactable NPC to talk to: a standing chara (sprite + script, action/talk boot).
const Event* Field::npcAt(int c, int r) const {
    for (const auto& a : actors_) {
        if (a.evIndex < 0 || a.evIndex >= (int)map_->events.size()) continue;
        const Event& e = map_->events[a.evIndex];
        if (a.col == c && a.row == r && !e.scripts.empty() && isStandingChara(e)
            && a.visible && appears(e))
            return &e;
    }
    return nullptr;
}

// A step-on trigger: a position-trigger event (boot 2/3/6/7/8) with a script.  Fires
// when the player lands on its tile; the script's 0x6b/0x41 supplies the warp dest.
const Event* Field::stepTriggerAt(int c, int r) const {
    for (const auto& e : map_->events)
        if (inRect(e, c, r) && isStepTrigger(e) && appears(e)) return &e;
    return nullptr;
}

// --- auto events (map-load cutscenes, parallels) ----------------------------
int Field::evIndex(const Event* e) const { return (int)(e - map_->events.data()); }

bool Field::canAutoRun(const Event* e) const {
    int i = evIndex(e);
    if (i < 0 || i >= (int)map_->events.size()) return false;
    if (autoBudget_ <= 0) return false;
    return runCount_.empty() || runCount_[i] < 4;        // per-event loop guard
}

void Field::queueAuto(const Event* e) {
    for (const Event* q : autoQueue_) if (q == e) return;
    autoQueue_.push_back(e);
}

// On-map-entry autos: parallels (boot 4/5) whose appear passes, boot-0 scripts
// (ambient setup, provisional: once), and boot-7 range events containing the
// spawn cell (this is how the m0/m9 prologue cutscenes start).
void Field::enterMap() {
    runCount_.assign(map_->events.size(), 0);
    autoQueue_.clear();
    autoBudget_ = 32;
    buildActors();
    lookActorId_ = -2;
    playerCmds_.clear(); playerCmdIdx_ = 0; playerWait_ = 0;
    wait_ = PendingWait{};
    resumeStack_.clear();
    for (const auto& e : map_->events) {
        if (e.scripts.empty() || !appears(e)) continue;
        if (isParallel(e) || e.boot == 0) queueAuto(&e);
        else if (e.boot == 7 && inRect(e, col_, row_)) queueAuto(&e);
    }
    if (!autoQueue_.empty())
        std::printf("[FFSmith] enterMap: %zu auto event(s) queued\n", autoQueue_.size());
}

// Re-scan parallels after flag/var writes (UpdateEventAppear): a boot-4/5
// event becomes eligible the moment its appear conditions pass.
void Field::rescanParallel() {
    for (const auto& e : map_->events)
        if (isParallel(e) && appears(e) && canAutoRun(&e)) queueAuto(&e);
}

void Field::pumpAuto() {
    if (dlgActive_ || choiceActive_ || sentenceActive_ || warp_.valid() || pendingEnc_.valid()
        || wait_.valid() || autoQueue_.empty()) return;
    const Event* e = autoQueue_.front();
    autoQueue_.erase(autoQueue_.begin());
    if (!appears(*e) || !canAutoRun(e)) return;
    int i = evIndex(e);
    if (i >= 0 && i < (int)runCount_.size()) ++runCount_[i];
    --autoBudget_;
    std::printf("[FFSmith] auto event ev%d (boot %d) runs\n", i, e->boot);
    bool wasDirty = script_ ? script_->dirty : false;
    if (script_) script_->dirty = false;
    runScript(e, 0);
    if (script_ && script_->dirty) rescanParallel();
    else if (script_) script_->dirty = wasDirty;
}

int Field::dialogueMsg() const {
    return (dlgActive_ && dlgIdx_ < (int)dlgQueue_.size()) ? dlgQueue_[dlgIdx_] : -1;
}

// Run an event's script (or resume after a choice) and absorb the result.
void Field::runScript(const Event* e, int startBlock) {
    static ScriptState fallback;               // debug paths without a Host state
    static const VMEnv kNullEnv{};
    ScriptState& st = script_ ? *script_ : fallback;
    VMOut o = run_event(*e, st, env_ ? *env_ : kNullEnv, startBlock);
    if (!o.messages.empty()) {
        for (int m : o.messages) dlgQueue_.push_back(m);
        if (!dlgActive_) { dlgIdx_ = 0; dlgActive_ = true; }
        std::printf("[FFSmith] script (img %d) -> msg %d (+%d)\n",
                    e->img, dlgQueue_[dlgIdx_], (int)o.messages.size() - 1);
    }
    if (!o.sentences.empty()) {                  // ScriptSentence: accumulate full-screen narration lines
        for (int sline : o.sentences) sentences_.push_back(sline);
        sentenceActive_ = true;
        std::printf("[FFSmith] script (img %d) -> sentence %d (+%d lines)\n",
                    e->img, sentences_.front(), (int)o.sentences.size() - 1);
    }
    if (o.hasChoice) {
        pendingEv_ = o.choiceEv ? o.choiceEv : e; choice_ = o.choice; choiceSel_ = 0;
        if (!dlgActive_) choiceActive_ = true;   // else: shown when dialogue drains
    }
    if (o.warpMap >= 0) {
        warp_ = {o.warpMap, o.warpX, o.warpY, o.warpDir};
        std::printf("[FFSmith] script warp -> map %d @(%d,%d)\n", o.warpMap, o.warpX, o.warpY);
    }
    if (o.hasEncounter) {
        pendingEnc_ = o.enc;
        if (!pendingEnc_.ev) pendingEnc_.ev = e;
        encLaunched_ = false;
        std::printf("[FFSmith] script encounter -> formation %d (resume blk %d)\n",
                    pendingEnc_.formation, pendingEnc_.resumeBlock);
    }
    // --- cutscene side effects ---
    for (const auto& act : o.actions) {
        Actor* a = actorById(act.id);
        if (a) {
            a->cmds = act.cmds; a->cmdIdx = 0;
            std::printf("[FFSmith] actor %d: %zu command(s)\n", act.id, a->cmds.size());
        } else {
            // Unknown entity id: assume it targets the PLAYER (the hero is
            // moved by 0x68 in cutscenes; his event id isn't in the map pack).
            // APPROXIMATION — log so wrong guesses are visible.
            playerCmds_ = act.cmds; playerCmdIdx_ = 0;
            std::printf("[FFSmith] actor %d not found -> commands drive the PLAYER (%zu cmds)\n",
                        act.id, act.cmds.size());
        }
    }
    for (const auto& t : o.teleports) {
        Actor* a = actorById(t.id);
        if (!a) continue;
        if (t.x >= 0) a->col = std::min(t.x, map_->w - 1);
        if (t.y >= 0) a->row = std::min(t.y, map_->h - 1);
        if (t.dir >= 0 && t.dir < 4) a->facing = t.dir;
        a->moving = false; a->prog = 0;
    }
    for (const auto& v : o.visibles) {
        Actor* a = actorById(v.first);
        if (a) { a->visible = v.second != 0; a->alpha = a->visible ? 255 : 0; a->fade = 0; }
    }
    if (o.cameraTarget != -2) lookActorId_ = o.cameraTarget;
    if (o.hasPlayerSet) {
        if (o.playerX >= 0 && o.playerX < map_->w) col_ = o.playerX;
        if (o.playerY >= 0 && o.playerY < map_->h) row_ = o.playerY;
        if (o.playerDir >= 0 && o.playerDir < 4) facing_ = o.playerDir;
        moving_ = false; prog_ = 0;
    }
    if (o.fadeMode >= 0 && env_ && env_->setFade)
        env_->setFade(o.fadeMode, o.fadeR, o.fadeG, o.fadeB, o.fadeTicks);
    if (o.pauseBlock >= 0) {
        wait_.ev = o.pauseEv ? o.pauseEv : e;
        wait_.block = o.pauseBlock;
        wait_.ticks = o.waitTicks;
        wait_.actors = o.waitActors;
        std::printf("[FFSmith] script waits (%s) -> resume blk %d\n",
                    o.waitActors ? "actors" : "ticks", o.pauseBlock);
    }
    if (!o.resumeStack.empty())                  // suspended 0x66 caller frames
        resumeStack_.insert(resumeStack_.end(), o.resumeStack.begin(), o.resumeStack.end());
}

// After a pause resumes and its run finishes without pausing again, pop and
// continue any suspended 0x66 caller frames (innermost-first = back of stack).
void Field::continueChain() {
    while (!scriptPaused() && !resumeStack_.empty()) {
        const Event* e = resumeStack_.back().first;
        int blk = resumeStack_.back().second;
        resumeStack_.pop_back();
        runScript(e, blk);
    }
}

Actor* Field::actorById(int id) {
    for (auto& a : actors_) if (a.id == id) return &a;
    return nullptr;
}

bool Field::actorsIdle() const {
    for (const auto& a : actors_) if (a.active()) return false;
    return !(playerCmdIdx_ < playerCmds_.size() || playerWait_ > 0 || moving_);
}

bool Field::lookTargetPixel(int& px, int& py) const {
    if (lookActorId_ == -2) return false;
    for (const auto& a : actors_)
        if (a.id == lookActorId_) { px = a.pixelX(tile_); py = a.pixelY(tile_); return true; }
    return false;                                // unknown id -> player
}

void Field::buildActors() {
    actors_.clear();
    for (size_t i = 0; i < map_->events.size(); ++i) {
        const Event& e = map_->events[i];
        Actor a;
        a.evIndex = (int)i; a.id = e.id;
        // FFM6: spawn = rect origin + offset; rect = wander bounds
        // (InitEventDataOfChara: x = rec[2]+rec[0x39], y = rec[3]+rec[0x3a]).
        a.col = e.x + e.off_x; a.row = e.y + e.off_y;
        a.facing = (e.facing0 >= 0 && e.facing0 < 4) ? e.facing0 : FACE_DOWN;
        a.moveType = e.move_type; a.wSpeed = e.speed0; a.wFreq = e.freq0;
        a.homeX = e.x; a.homeY = e.y; a.homeW = e.w; a.homeH = e.h;
        actors_.push_back(a);
    }
}

// Interpret one command byte for an actor (table DAT_00418d40; see field.h).
void Field::applyCommandTo(Actor& a, int cmd) {
    static const int DXC[8] = {0, 0, -1, 1, -1, 1, -1, 1};   // D U L R + diagonals
    static const int DYC[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int TURN_L[4] = {2, 3, 1, 0};               // DAT_001798c0 pairs
    static const int TURN_R[4] = {3, 2, 0, 1};
    auto facePlayer = [&](bool toward) {
        int dx = col_ - a.col, dy = row_ - a.row;
        int f;
        if (std::abs(dx) > std::abs(dy)) f = dx < 0 ? FACE_LEFT : FACE_RIGHT;
        else f = dy < 0 ? FACE_UP : FACE_DOWN;
        if (!toward) f ^= 1;                       // reverse (D<->U, L<->R pair-flip)
        a.facing = f;
    };
    auto walk = [&](int dir8) {
        a.dx = DXC[dir8 & 7]; a.dy = DYC[dir8 & 7];
        if (dir8 < 4) a.facing = dir8;
        else a.facing = (a.dx < 0) ? FACE_LEFT : FACE_RIGHT;
        int nc = a.col + a.dx, nr = a.row + a.dy;
        // scripted moves don't collide (cutscene paths are authored clear) but stay in bounds
        if (nc < 0 || nr < 0 || nc >= map_->w || nr >= map_->h) return;
        a.tcol = nc; a.trow = nr; a.prog = 0; a.moving = true;
    };
    if (cmd <= 0x07) { walk(cmd); return; }                       // walk 8 dirs
    if (cmd == 0x08) { facePlayer(true); walk(a.facing); return; }  // step toward player
    if (cmd == 0x09) { facePlayer(false); walk(a.facing); return; } // step away
    if (cmd == 0x0a) { walk(a.facing); return; }                  // step ahead
    if (cmd == 0x0b) { a.waitTicks = tile_ / std::max(1, a.speed); return; }   // pause one beat
    if (cmd == 0x0c) { a.fade = -1; return; }                     // fade out
    if (cmd == 0x0d) { a.fade = +1; a.visible = true; return; }   // fade in
    if (cmd >= 0x10 && cmd <= 0x13) { a.facing = cmd - 0x10; return; }
    if (cmd >= 0x14 && cmd <= 0x19) {                             // turn left/right (3 pairs)
        a.facing = ((cmd - 0x14) & 1) ? TURN_R[a.facing & 3] : TURN_L[a.facing & 3];
        return;
    }
    if (cmd == 0x1a || cmd == 0x1b) {                             // random turn
        a.facing = (std::rand() & 1) ? TURN_L[a.facing & 3] : TURN_R[a.facing & 3];
        return;
    }
    if (cmd == 0x1c) { facePlayer(true); return; }
    if (cmd == 0x1d) { facePlayer(false); return; }
    if (cmd >= 0x20 && cmd <= 0x24) { a.alpha = std::min(255, (cmd - 0x20) * 64); return; }
    if (cmd >= 0x25 && cmd <= 0x29) { a.speed = 1 << std::min(3, cmd - 0x25); return; }
    if (cmd >= 0x2a && cmd <= 0x2d) { return; }                   // anim frequency (cosmetic)
    if (cmd == 0x30 || cmd == 0x32) { walk(a.facing); return; }   // jump ~ walk (APPROX)
    if (cmd >= 0x40 && cmd <= 0x44) { return; }                   // pose (not rendered yet)
    if (cmd == 0x45) { a.waitTicks = tile_; return; }             // long pause
    if (cmd >= 0x80 && cmd <= 0x8d) {                             // chara flag set/clear
        int idx = (cmd - 0x80) >> 1; bool clear = (cmd & 1) != 0;
        if (idx == 4) { a.visible = !clear; a.alpha = a.visible ? 255 : 0; }   // flag 0x400
        else if (idx == 2) a.animOn = !clear;                                  // flag 0x2
        return;
    }
    // 0x90 face-current = no-op; 0x91 and unknowns: skip (logged once per run elsewhere)
}

void Field::tickActors() {
    for (auto& a : actors_) {
        if (a.fade != 0) {
            a.alpha += a.fade * 16;
            if (a.alpha <= 0)   { a.alpha = 0; a.fade = 0; a.visible = false; }
            if (a.alpha >= 255) { a.alpha = 255; a.fade = 0; }
        }
        if (a.waitTicks > 0) { --a.waitTicks; continue; }
        if (a.moving) {
            a.prog += a.speed;
            if (a.prog >= tile_) { a.col = a.tcol; a.row = a.trow; a.prog = 0; a.moving = false; }
            continue;
        }
        while (a.cmdIdx < a.cmds.size()) {                 // consume instant commands
            int cmd = a.cmds[a.cmdIdx++];
            applyCommandTo(a, cmd);
            if (a.moving || a.waitTicks > 0 || a.fade != 0) break;
        }
        if (!a.active()) tickWander(a);                    // NPC auto-wander when idle
    }
    // scripted player commands (0x68 fallback)
    if (playerWait_ > 0) { --playerWait_; }
    else if (!moving_ && playerCmdIdx_ < playerCmds_.size()) {
        static const int DXC[8] = {0, 0, -1, 1, -1, 1, -1, 1};
        static const int DYC[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        int cmd = playerCmds_[playerCmdIdx_++];
        if (cmd <= 0x07) {
            int nc = col_ + DXC[cmd & 7], nr = row_ + DYC[cmd & 7];
            if (cmd < 4) facing_ = cmd;
            if (nc >= 0 && nr >= 0 && nc < map_->w && nr < map_->h) {
                tcol_ = nc; trow_ = nr; moveDir_ = (cmd < 4) ? cmd : facing_;
                prog_ = 0; moving_ = true;
            }
        } else if (cmd >= 0x10 && cmd <= 0x13) facing_ = cmd - 0x10;
        else if (cmd == 0x0b) playerWait_ = tile_ / 2;
        else if (cmd == 0x45) playerWait_ = tile_;
    }
}

// NPC step collision: map pass grid + other actors (isSolid) + the player's
// current and in-flight tile (original: CheckMovePass GetCharaOfPosition +
// MoveCharaPassiveHit player checks).
bool Field::wanderBlocked(int c, int r) const {
    if (isSolid(c, r)) return true;
    if (c == col_ && r == row_) return true;
    if (moving_ && c == tcol_ && r == trow_) return true;
    return false;
}

// FieldClass::MoveCharaAuto (c:115518) approximation.  Differences from the
// original, flagged MEDIUM: (1) the original stops only the NPC whose event is
// active (CheckEventActive); we pause ALL wander while any script/dialogue is
// pending so 0x69 actor-waits can settle.  (2) a blocked pick gets the same
// wander pause as a walk (the original's face-command duration is undecoded).
void Field::tickWander(Actor& a) {
    if (a.moveType < 2) return;                       // 0/1 = stand
    if (dlgActive_ || choiceActive_ || sentenceActive_ || scriptPaused()) return;
    if (a.evIndex < 0 || a.evIndex >= (int)map_->events.size()) return;
    const Event& e = map_->events[a.evIndex];
    if (e.img <= 0 || !a.visible || !appears(e)) return;
    if (a.wanderWait > 0) { --a.wanderWait; return; }
    // candidate dirs: target stays inside the event rect (move_type 2 only;
    // GetPassFlags c:117339 low bits) -- blocked dirs stay candidates.
    int cand[4], n = 0;
    for (int d = 0; d < 4; ++d) {
        int nc = a.col + DX[d], nr = a.row + DY[d];
        if (a.moveType == 2 && (nc < a.homeX || nc >= a.homeX + a.homeW ||
                                nr < a.homeY || nr >= a.homeY + a.homeH)) continue;
        cand[n++] = d;
    }
    a.wanderWait = g_fieldConst.wanderWait(a.wFreq);
    if (!n) return;                                   // boxed in: wait (cmd 0x0b)
    int d = cand[std::rand() % n];                    // Rand(popcount) pick
    int nc = a.col + DX[d], nr = a.row + DY[d];
    a.facing = d;                                     // face the pick either way
    if (wanderBlocked(nc, nr)) return;                // hit bit set: turn only
    a.dx = DX[d]; a.dy = DY[d];
    a.tcol = nc; a.trow = nr; a.prog = 0; a.moving = true;
    a.speed = std::max(1, tile_ / g_fieldConst.walkDur(a.wSpeed));
}

void Field::tickWaits() {
    if (!wait_.valid() || dlgActive_ || choiceActive_) return;
    bool done = false;
    if (wait_.actors) done = actorsIdle();
    else if (wait_.ticks > 0) { --wait_.ticks; done = (wait_.ticks == 0); }
    else done = true;
    if (done) {
        const Event* e = wait_.ev; int blk = wait_.block;
        wait_ = PendingWait{};
        runScript(e, blk);
        continueChain();
    }
}

// The battle is over: continue the paused script at the block after 0x50.
// The script can read the result via GetReference target 8 (env.battleRef).
void Field::resumeAfterBattle() {
    if (!pendingEnc_.valid()) return;
    const Event* e = pendingEnc_.ev;
    int blk = pendingEnc_.resumeBlock;
    pendingEnc_ = VMEncounter{};
    encLaunched_ = false;
    if (e && blk >= 0) runScript(e, blk);
    continueChain();
}

void Field::confirm() {
    if (choiceActive_) {                    // pick the highlighted option, resume VM
        int blk = choice_.options.empty() ? choice_.defaultBlock
                                          : choice_.options[choiceSel_].second;
        const Event* e = pendingEv_;
        choiceActive_ = false; pendingEv_ = nullptr; choice_ = VMChoice{};
        std::printf("[FFSmith] choice %d -> blk %d\n", choiceSel_, blk);
        if (e) runScript(e, blk);
        continueChain();
        return;
    }
    if (sentenceActive_) {                  // dismiss the full-screen narration, then resume
        sentenceActive_ = false; sentences_.clear();
        return;
    }
    if (dlgActive_) {                       // advance / close dialogue
        ++dlgIdx_;
        if (dlgIdx_ >= (int)dlgQueue_.size()) {
            dlgActive_ = false; dlgQueue_.clear(); dlgIdx_ = 0;
            if (pendingEv_) choiceActive_ = true;   // queued choice after the text
        } else {
            std::printf("[FFSmith] dialogue: msg %d\n", dlgQueue_[dlgIdx_]);
        }
        return;
    }
    if (moving_) return;
    const Event* e = npcAt(col_ + DX[facing_], row_ + DY[facing_]);
    if (e) {                                // talked-to NPC turns to the player
        for (auto& a : actors_)             // (SetCharaLookDir on talk; MEDIUM)
            if (a.evIndex >= 0 && &map_->events[a.evIndex] == e && !a.moving)
                a.facing = facing_ ^ 1;     // pair-flip D<->U / L<->R
    }
    if (!e) {                               // boot 8 = confirm while inside the rect
        for (const auto& ev : map_->events)
            if (ev.boot == 8 && !ev.scripts.empty() && inRect(ev, col_, row_) && appears(ev)) {
                runScript(&ev, 0);
                return;
            }
        return;
    }
    runScript(e, 0);
}

void Field::cancel() {
    if (!choiceActive_) return;
    int blk = choice_.defaultBlock;
    const Event* e = pendingEv_;
    choiceActive_ = false; pendingEv_ = nullptr; choice_ = VMChoice{};
    if (e) runScript(e, blk);
    continueChain();
}

void Field::choiceMove(int d) {
    if (choice_.options.empty()) return;
    int n = (int)choice_.options.size();
    choiceSel_ = (choiceSel_ + d + n) % n;
}

void Field::update(const InputState& in) {
    tickActors();                           // cutscene actors + fades always tick
    tickWaits();                            // resume scripts whose wait elapsed
    pumpAuto();                             // run pending auto events when idle
    if (choiceActive_) {                    // choice menu eats input
        if (in.pressed & BTN_UP)    choiceMove(-1);
        if (in.pressed & BTN_DOWN)  choiceMove(1);
        if (in.pressed & BTN_CONFIRM) confirm();
        else if (in.pressed & BTN_CANCEL) cancel();
        return;
    }
    if (in.pressed & BTN_CONFIRM) confirm();
    if (dlgActive_ || choiceActive_ || sentenceActive_) return;   // freeze movement during dialogue/narration
    if (moving_) {
        prog_ += speed_;
        if (prog_ >= tile_) {
            col_ = tcol_; row_ = trow_; prog_ = 0; moving_ = false;
            const Event* t = stepTriggerAt(col_, row_);   // step-on trigger fires on arrival
            if (t) {
                std::printf("[FFSmith] step trigger @(%d,%d) boot=0x%02x\n", col_, row_, t->boot);
                runScript(t, 0);
            }
        }
    }
    if (!moving_ && !playerScripted()) {
        int d = dirFromHeld(in.held);
        if (d >= 0) {
            facing_ = d;
            int nc = col_ + DX[d], nr = row_ + DY[d];
            if (!isSolid(nc, nr)) { tcol_ = nc; trow_ = nr; moveDir_ = d; prog_ = 0; moving_ = true; }
        }
    }
}

int Field::pixelX() const { int b = col_ * tile_; if (moving_) b += DX[moveDir_] * prog_; return b; }
int Field::pixelY() const { int b = row_ * tile_; if (moving_) b += DY[moveDir_] * prog_; return b; }

int Field::animCol() const {
    if (!moving_) return 0;                      // idle
    static const int SEQ[4] = {1, 0, 2, 0};      // walkA, idle, walkB, idle
    int t = tile_ > 0 ? tile_ : 32;
    int ph = (prog_ * 4) / t; if (ph < 0) ph = 0; if (ph > 3) ph = 3;
    return SEQ[ph];
}

}  // namespace ffsmith
