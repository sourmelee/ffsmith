#include "field/field.h"
#include "field/event_vm.h"
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

bool Field::isSolid(int c, int r) const {
    if (c < 0 || r < 0 || c >= map_->w || r >= map_->h) return true;
    // Standing NPCs/objects block movement; position triggers (incl. visible door
    // sprites) are walkable so you can step onto them and warp.
    for (const auto& e : map_->events)
        if (e.x == c && e.y == r && isStandingChara(e)) return true;
    if (map_->pass.empty()) return false;
    uint8_t nib = map_->pass[(size_t)r * map_->w + c];
    return (nib & 0x0f) == 0;
}

// An interactable NPC to talk to: a standing chara (sprite + script, action/talk boot).
const Event* Field::npcAt(int c, int r) const {
    for (const auto& e : map_->events)
        if (e.x == c && e.y == r && !e.scripts.empty() && isStandingChara(e)) return &e;
    return nullptr;
}

// A step-on trigger: a position-trigger event (boot 2/3/6/7/8) with a script.  Fires
// when the player lands on its tile; the script's 0x6b/0x41 supplies the warp dest.
const Event* Field::stepTriggerAt(int c, int r) const {
    for (const auto& e : map_->events)
        if (e.x == c && e.y == r && isStepTrigger(e)) return &e;
    return nullptr;
}

int Field::dialogueMsg() const {
    return (dlgActive_ && dlgIdx_ < (int)dlgQueue_.size()) ? dlgQueue_[dlgIdx_] : -1;
}

void Field::confirm() {
    if (dlgActive_) {                       // advance / close dialogue
        ++dlgIdx_;
        if (dlgIdx_ >= (int)dlgQueue_.size()) { dlgActive_ = false; dlgQueue_.clear(); dlgIdx_ = 0; }
        else std::printf("[FFSmith] dialogue: msg %d\n", dlgQueue_[dlgIdx_]);
        return;
    }
    if (moving_) return;
    const Event* e = npcAt(col_ + DX[facing_], row_ + DY[facing_]);
    if (!e) return;
    VMOut o = run_event(*e);
    if (o.warpMap >= 0) {
        warp_ = {o.warpMap, o.warpX, o.warpY, o.warpDir};
    } else if (!o.messages.empty()) {
        dlgQueue_ = o.messages; dlgIdx_ = 0; dlgActive_ = true;
        std::printf("[FFSmith] talk (img %d) -> msg %d\n", e->img, dlgQueue_[0]);
    }
}

void Field::update(const InputState& in) {
    if (in.pressed & BTN_CONFIRM) confirm();
    if (dlgActive_) return;                 // freeze movement during dialogue
    if (moving_) {
        prog_ += speed_;
        if (prog_ >= tile_) {
            col_ = tcol_; row_ = trow_; prog_ = 0; moving_ = false;
            const Event* t = stepTriggerAt(col_, row_);   // step-on trigger fires on arrival
            if (t) {
                VMOut o = run_event(*t);
                if (o.warpMap >= 0) {
                    warp_ = {o.warpMap, o.warpX, o.warpY, o.warpDir};
                    std::printf("[FFSmith] step trigger @(%d,%d) boot=0x%02x -> warp map %d @(%d,%d)\n",
                                col_, row_, t->boot, o.warpMap, o.warpX, o.warpY);
                } else if (!o.messages.empty()) {
                    dlgQueue_ = o.messages; dlgIdx_ = 0; dlgActive_ = true;
                    std::printf("[FFSmith] step trigger @(%d,%d) boot=0x%02x -> msg %d\n",
                                col_, row_, t->boot, dlgQueue_[0]);
                }
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
