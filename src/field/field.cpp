#include "field/field.h"

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
    if (c < 0 || r < 0 || c >= map_->w || r >= map_->h) return true;   // off-map = solid
    if (map_->pass.empty()) return false;                              // no collision data: open
    uint8_t nib = map_->pass[(size_t)r * map_->w + c];
    return (nib & 0x0f) == 0;   // all 4 directions blocked => wall/solid (capk.dat)
}

void Field::update(const InputState& in) {
    if (moving_) {
        prog_ += speed_;
        if (prog_ >= tile_) { col_ = tcol_; row_ = trow_; prog_ = 0; moving_ = false; }
    }
    if (!moving_) {
        int d = dirFromHeld(in.held);
        if (d >= 0) {
            facing_ = d;
            int nc = col_ + DX[d], nr = row_ + DY[d];
            if (!isSolid(nc, nr)) {           // bounds + wall collision (capk)
                tcol_ = nc; trow_ = nr; moveDir_ = d; prog_ = 0; moving_ = true;
            }
        }
    }
}

int Field::pixelX() const {
    int base = col_ * tile_;
    if (moving_) base += DX[moveDir_] * prog_;
    return base;
}
int Field::pixelY() const {
    int base = row_ * tile_;
    if (moving_) base += DY[moveDir_] * prog_;
    return base;
}

}  // namespace ffsmith
