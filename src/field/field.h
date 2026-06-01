#pragma once
#include <cstdint>
#include "data/bundle.h"
#include "host/input.h"

namespace ffsmith {

// Facing / move directions. Index order is FFSmith's own; the original's exact
// s_MoveDirToMoves indexing is a static table not present in the decompilation,
// but 4-dir grid movement behaviour is identical.
enum Facing { FACE_DOWN = 0, FACE_UP = 1, FACE_LEFT = 2, FACE_RIGHT = 3 };

// Field-mode player state + grid movement. SDL-free so it can be traced
// headlessly (--walk). Movement mirrors FieldClass::GetAfterPositionOfWalk:
// one tile per step, target clamped to map bounds (wrap maps = M2.1).
// Tile/chip wall collision is M2.1 — isSolid() is the hook.
class Field {
public:
    Field(const FfMap* map, int tile, int startCol, int startRow);

    void update(const InputState& in);   // advance one logic tick

    int  col() const { return col_; }
    int  row() const { return row_; }
    int  facing() const { return facing_; }
    bool moving() const { return moving_; }
    int  tile() const { return tile_; }
    int  pixelX() const;                  // player top-left, map pixels
    int  pixelY() const;

    bool isSolid(int col, int row) const; // M2.1 chip-attribute collision hook

private:
    const FfMap* map_;
    int tile_;
    int col_, row_;
    int tcol_ = 0, trow_ = 0;   // step target
    int facing_ = FACE_DOWN;
    int moveDir_ = 0;
    bool moving_ = false;
    int prog_ = 0;              // pixels into current step (0..tile_)
    int speed_ = 2;            // px per tick (tile_/speed_ ticks per tile)
};

}  // namespace ffsmith
