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

bool Field::isSolid(int c, int r) const {
    if (c < 0 || r < 0 || c >= map_->w || r >= map_->h) return true;
    for (const auto& e : map_->events)        // visible NPCs block movement
        if (e.img > 0 && e.x == c && e.y == r) return true;
    if (map_->pass.empty()) return false;
    uint8_t nib = map_->pass[(size_t)r * map_->w + c];
    return (nib & 0x0f) == 0;
}

const Event* Field::npcAt(int c, int r) const {
    for (const auto& e : map_->events)
        if (e.x == c && e.y == r && !e.scripts.empty()) return &e;
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
    if (!o.messages.empty()) {
        dlgQueue_ = o.messages; dlgIdx_ = 0; dlgActive_ = true;
        std::printf("[FFSmith] talk (img %d) -> msg %d\n", e->img, dlgQueue_[0]);
    }
}

void Field::update(const InputState& in) {
    if (in.pressed & BTN_CONFIRM) confirm();
    if (dlgActive_) return;                 // freeze movement during dialogue
    if (moving_) {
        prog_ += speed_;
        if (prog_ >= tile_) { col_ = tcol_; row_ = trow_; prog_ = 0; moving_ = false; }
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

}  // namespace ffsmith
