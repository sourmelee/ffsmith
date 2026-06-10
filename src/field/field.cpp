#include "field/field.h"
#include <cstdio>

namespace ffsmith {

static const int DX[4] = {0, 0, -1, 1};  // DOWN, UP, LEFT, RIGHT
static const int DY[4] = {1, -1, 0, 0};

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
static bool isStepTrigger(const Event& e) {
    return !e.scripts.empty() &&
           (e.boot == 2 || e.boot == 3 || e.boot == 6 || e.boot == 7 || e.boot == 8);
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
    // Standing NPCs/objects block movement; position triggers (incl. visible door
    // sprites) are walkable so you can step onto them and warp.
    for (const auto& e : map_->events)
        if (e.x == c && e.y == r && isStandingChara(e) && appears(e)) return true;
    if (map_->pass.empty()) return false;
    uint8_t nib = map_->pass[(size_t)r * map_->w + c];
    return (nib & 0x0f) == 0;
}

// An interactable NPC to talk to: a standing chara (sprite + script, action/talk boot).
const Event* Field::npcAt(int c, int r) const {
    for (const auto& e : map_->events)
        if (e.x == c && e.y == r && !e.scripts.empty() && isStandingChara(e) && appears(e))
            return &e;
    return nullptr;
}

// A step-on trigger: a position-trigger event (boot 2/3/6/7/8) with a script.  Fires
// when the player lands on its tile; the script's 0x6b/0x41 supplies the warp dest.
const Event* Field::stepTriggerAt(int c, int r) const {
    for (const auto& e : map_->events)
        if (e.x == c && e.y == r && isStepTrigger(e) && appears(e)) return &e;
    return nullptr;
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
    if (o.hasChoice) {
        pendingEv_ = e; choice_ = o.choice; choiceSel_ = 0;
        if (!dlgActive_) choiceActive_ = true;   // else: shown when dialogue drains
    }
    if (o.warpMap >= 0) {
        warp_ = {o.warpMap, o.warpX, o.warpY, o.warpDir};
        std::printf("[FFSmith] script warp -> map %d @(%d,%d)\n", o.warpMap, o.warpX, o.warpY);
    }
}

void Field::confirm() {
    if (choiceActive_) {                    // pick the highlighted option, resume VM
        int blk = choice_.options.empty() ? choice_.defaultBlock
                                          : choice_.options[choiceSel_].second;
        const Event* e = pendingEv_;
        choiceActive_ = false; pendingEv_ = nullptr; choice_ = VMChoice{};
        std::printf("[FFSmith] choice %d -> blk %d\n", choiceSel_, blk);
        if (e) runScript(e, blk);
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
    if (!e) return;
    runScript(e, 0);
}

void Field::cancel() {
    if (!choiceActive_) return;
    int blk = choice_.defaultBlock;
    const Event* e = pendingEv_;
    choiceActive_ = false; pendingEv_ = nullptr; choice_ = VMChoice{};
    if (e) runScript(e, blk);
}

void Field::choiceMove(int d) {
    if (choice_.options.empty()) return;
    int n = (int)choice_.options.size();
    choiceSel_ = (choiceSel_ + d + n) % n;
}

void Field::update(const InputState& in) {
    if (choiceActive_) {                    // choice menu eats input
        if (in.pressed & BTN_UP)    choiceMove(-1);
        if (in.pressed & BTN_DOWN)  choiceMove(1);
        if (in.pressed & BTN_CONFIRM) confirm();
        else if (in.pressed & BTN_CANCEL) cancel();
        return;
    }
    if (in.pressed & BTN_CONFIRM) confirm();
    if (dlgActive_ || choiceActive_) return;   // freeze movement during dialogue
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
    if (!moving_) {
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
