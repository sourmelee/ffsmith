#pragma once
#include <cstdint>
#include <vector>
#include "data/bundle.h"
#include "host/input.h"

namespace ffsmith {

enum Facing { FACE_DOWN = 0, FACE_UP = 1, FACE_LEFT = 2, FACE_RIGHT = 3 };

class Field {
public:
    Field(const FfMap* map, int tile, int startCol, int startRow);

    void update(const InputState& in);

    int  col() const { return col_; }
    int  row() const { return row_; }
    int  facing() const { return facing_; }
    bool moving() const { return moving_; }
    int  tile() const { return tile_; }
    int  pixelX() const;
    int  pixelY() const;

    bool isSolid(int c, int r) const;
    const FfMap* map() const { return map_; }

    // events / dialogue
    bool inDialogue() const { return dlgActive_; }
    int  dialogueMsg() const;
    void confirm();                              // talk / advance dialogue
    const Event* npcAt(int c, int r) const;      // event with scripts at a tile

private:
    const FfMap* map_;
    int tile_;
    int col_, row_;
    int tcol_ = 0, trow_ = 0;
    int facing_ = FACE_DOWN;
    int moveDir_ = 0;
    bool moving_ = false;
    int prog_ = 0;
    int speed_ = 2;
    std::vector<int> dlgQueue_;
    int dlgIdx_ = 0;
    bool dlgActive_ = false;
};

}  // namespace ffsmith
