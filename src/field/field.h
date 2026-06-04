#pragma once
#include <cstdint>
#include <vector>
#include "data/bundle.h"
#include "host/input.h"

namespace ffsmith {

enum Facing { FACE_DOWN = 0, FACE_UP = 1, FACE_LEFT = 2, FACE_RIGHT = 3 };

struct Warp { int map = -1, x = 0, y = 0, dir = -1; bool valid() const { return map >= 0; } };

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
    int  animCol() const;   // 0=idle,1=walkA,2=walkB (walk cycle while moving)

    bool isSolid(int c, int r) const;
    const FfMap* map() const { return map_; }

    // events / dialogue
    bool inDialogue() const { return dlgActive_; }
    int  dialogueMsg() const;
    void confirm();                              // talk / advance dialogue
    Warp consumeWarp() { Warp w = warp_; warp_ = Warp{}; return w; }  // pending cross-map warp
    void setNoClip(bool b) { noClip_ = b; }     // debug: ignore collision
    bool noClip() const { return noClip_; }
    void setFacing(int f) { if (f >= 0 && f < 4) facing_ = f; }
    void openMessage(int id, int count = 1) {    // debug/scripted: open dialogue at msg id..id+count-1
        dlgQueue_.clear();
        for (int k = 0; k < count; ++k) dlgQueue_.push_back(id + k);
        dlgIdx_ = 0; dlgActive_ = true;
    }
    const Event* npcAt(int c, int r) const;      // talk target at a tile
    const Event* stepTriggerAt(int c, int r) const;  // step-on trigger at a tile

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
    Warp warp_;
    bool noClip_ = false;
    std::vector<int> dlgQueue_;
    int dlgIdx_ = 0;
    bool dlgActive_ = false;
};

}  // namespace ffsmith
