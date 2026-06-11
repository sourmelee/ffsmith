#pragma once
#include <cstdint>
#include <utility>
#include <vector>
#include "data/bundle.h"
#include "field/event_vm.h"
#include "field/script_state.h"
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
    void setPos(int c, int r) { col_ = c; row_ = r; moving_ = false; prog_ = 0; }
    int  facing() const { return facing_; }
    bool moving() const { return moving_; }
    int  tile() const { return tile_; }
    int  pixelX() const;
    int  pixelY() const;
    int  animCol() const;   // 0=idle,1=walkA,2=walkB (walk cycle while moving)

    bool isSolid(int c, int r) const;
    const FfMap* map() const { return map_; }

    // script state (flags/vars; owned by Host, shared across maps)
    void setScript(ScriptState* st, const VMEnv* env) { script_ = st; env_ = env; }
    ScriptState* script() const { return script_; }
    bool appears(const Event& e) const;          // CheckEventAppear gate

    // events / dialogue / choices
    bool inDialogue() const { return dlgActive_ || choiceActive_; }
    int  dialogueMsg() const;
    bool choiceActive() const { return choiceActive_; }
    const std::vector<std::pair<int, int>>& choiceOptions() const { return choice_.options; }
    int  choiceSel() const { return choiceSel_; }
    void confirm();                              // talk / advance dialogue / pick choice
    void cancel();                               // choice: take the default branch
    Warp consumeWarp() {                         // pending cross-map warp; dialogue first
        if (dlgActive_ || choiceActive_) return Warp{};
        Warp w = warp_; warp_ = Warp{}; return w;
    }
    void setNoClip(bool b) { noClip_ = b; }     // debug: ignore collision
    bool noClip() const { return noClip_; }
    void setFacing(int f) { if (f >= 0 && f < 4) facing_ = f; }
    void openMessage(int id, int count = 1) {    // debug/scripted: open dialogue at msg id..id+count-1
        dlgQueue_.clear();
        for (int k = 0; k < count; ++k) dlgQueue_.push_back(id + k);
        dlgIdx_ = 0; dlgActive_ = true;
    }
    const Event* npcAt(int c, int r) const;      // talk target at a tile
    const Event* stepTriggerAt(int c, int r) const;  // step/range trigger at a tile
    void enterMap();                             // fire on-load autos (boot 4/5/0 + boot-7 rect)

    // Scripted battles (0x50 ScriptEncount): the VM pauses; the host starts
    // the battle, then resumeAfterBattle() continues the script.
    bool encounterPending() const { return pendingEnc_.valid() && !encLaunched_; }
    VMEncounter startEncounter() { encLaunched_ = true; return pendingEnc_; }
    void resumeAfterBattle();                    // run the paused script's resume block
    void debugRunEvent(const Event* e) { runScript(e, 0); }   // self-tests

private:
    void runScript(const Event* e, int startBlock);   // run VM + absorb VMOut
    void choiceMove(int d);
    int  evIndex(const Event* e) const;
    bool canAutoRun(const Event* e) const;
    void queueAuto(const Event* e);
    void pumpAuto();                             // run queued autos when idle
    void rescanParallel();                       // boot 4/5: run when appear passes

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
    ScriptState* script_ = nullptr;
    const VMEnv* env_ = nullptr;
    std::vector<int> dlgQueue_;
    int dlgIdx_ = 0;
    bool dlgActive_ = false;
    bool choiceActive_ = false;                  // 0x3c pause: pick then resume VM
    VMChoice choice_;
    int choiceSel_ = 0;
    const Event* pendingEv_ = nullptr;           // event awaiting choice resume
    VMEncounter pendingEnc_;                     // 0x50 battle awaiting start/resume
    bool encLaunched_ = false;
    std::vector<const Event*> autoQueue_;        // pending auto-run events
    std::vector<int> runCount_;                  // per-event auto-run count (loop guard)
    int autoBudget_ = 32;                        // total auto runs per map visit
};

}  // namespace ffsmith
