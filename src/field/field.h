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

// A movable map actor (one per event). Driven by 0x68 command bytes — the
// engine's command table DAT_00418d40 (extracted from libjniproxy.so):
//   0x00-0x07 walk D/U/L/R + 4 diagonals     0x08-0x0a walk toward/away/ahead
//   0x0b pause beat   0x0c/0x0d fade out/in   0x10-0x13 face D/U/L/R
//   0x14-0x19 turn left/right   0x1a/0x1b random face   0x1c face player
//   0x1d face away   0x20-0x24 set alpha   0x25-0x29 set speed
//   0x2a-0x2d anim frequency   0x30/0x32 jump (approx walk)   0x40-0x44 pose
//   0x45 long pause   0x80-0x8d chara-flag set/clear (incl. 0x88/0x89 show/hide)
//   0x90 face current   0x91 (unknown, skipped)
struct Actor {
    int evIndex = -1;                // index into map()->events
    int id = 0;                      // event id (GetCharaOfEventID key)
    int col = 0, row = 0, facing = FACE_DOWN;
    int tcol = 0, trow = 0, prog = 0;  // in-flight step (pixels)
    int dx = 0, dy = 0;              // step direction (supports diagonals)
    bool moving = false;
    bool visible = true;             // chara flag 0x400
    bool animOn = true;              // chara flag 0x2
    int alpha = 255, fade = 0;       // fade: -1 out / +1 in (cmds 0x0c/0x0d)
    int speed = 2;                   // px/tick
    int waitTicks = 0;
    std::vector<uint8_t> cmds; size_t cmdIdx = 0;
    // NPC auto-wander (FieldClass::MoveCharaAuto c:115518): move_type 2 =
    // wander confined to the event rect, 3 = unbounded.  When idle, pick a
    // random direction among those whose target stays in the rect; a
    // collision-blocked pick just TURNS the NPC (GetPassFlags hit bits ->
    // face command 0x10|dir).  Cadence: walk step (field_constant walk-dur)
    // then wanderWait ticks (field_constant wait table by frequency; max
    // frequency = no pause).  Only the NPC whose event is running stops
    // (CheckEventActive c:138435); engine approximation pauses all wander
    // while a script/dialogue is pending.
    int moveType = 0;                // event move_type (header +0x35)
    int wSpeed = 2, wFreq = 2;       // walk-speed / frequency indexes
    int homeX = 0, homeY = 0, homeW = 1, homeH = 1;  // event rect = wander bounds
    int wanderWait = 0;              // ticks until the next wander think
    bool active() const { return moving || waitTicks > 0 || fade != 0 || cmdIdx < cmds.size(); }
    int pixelX(int tile) const { return col * tile + (moving ? dx * prog : 0); }
    int pixelY(int tile) const { return row * tile + (moving ? dy * prog : 0); }
    int animCol(int tile) const {                 // walk cycle (same as player)
        if (!moving || !animOn) return 0;
        static const int SEQ[4] = {1, 0, 2, 0};
        int t = tile > 0 ? tile : 32;
        int ph = (prog * 4) / t; if (ph < 0) ph = 0; if (ph > 3) ph = 3;
        return SEQ[ph];
    }
};

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
    // ScriptSentence (op 0x01): full-screen narration lines that accumulate and
    // are dismissed together -- separate from the windowed message box.
    bool inSentence() const { return sentenceActive_; }
    const std::vector<int>& sentenceLines() const { return sentences_; }
    int  dialogueMsg() const;
    bool choiceActive() const { return choiceActive_; }
    const std::vector<std::pair<int, int>>& choiceOptions() const { return choice_.options; }
    int  choiceSel() const { return choiceSel_; }
    void confirm();                              // talk / advance dialogue / pick choice
    void cancel();                               // choice: take the default branch
    Warp consumeWarp() {                         // pending cross-map warp; dialogue/waits first
        if (dlgActive_ || choiceActive_ || sentenceActive_ || wait_.valid()) return Warp{};
        Warp w = warp_; warp_ = Warp{}; return w;
    }
    void setNoClip(bool b) { noClip_ = b; }     // debug: ignore collision
    // NPC wander timing tables (data/field_constant.bin; decoded defaults
    // compiled in).  Static: one config per game, set once at startup.
    static void setFieldConstant(const FieldConstant& fc);
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

    // Cutscene actors (0x68/0x69/0x1b/0x20/0x21/0x55)
    const std::vector<Actor>& actors() const { return actors_; }
    Actor* actorById(int id);
    bool actorsIdle() const;                     // incl. scripted player moves
    bool lookTargetPixel(int& px, int& py) const;  // camera target (0x1b); false = player
    bool playerScripted() const { return playerCmdIdx_ < playerCmds_.size() || playerWait_ > 0; }
    // One-step scripted walk for the player. NOTE: a global "walk-in after
    // every warp" was tried and REVERTED — the intro's warps land EXACTLY on
    // the next dispatcher tile (m300 (1,1)/(1,2)/(1,3) beats), so stepping
    // overshoots story beats. m200's (1,0) spawn one tile above its trigger
    // is real: the player walks one step to continue (original pacing).
    void walkIn(int dir) {
        if (dir >= 0 && dir < 4) { facing_ = dir; playerCmds_ = { (uint8_t)dir }; playerCmdIdx_ = 0; }
    }

private:
    void runScript(const Event* e, int startBlock);   // run VM + absorb VMOut
    void buildActors();
    void tickActors();
    void tickWander(Actor& a);                   // MoveCharaAuto approximation
    bool wanderBlocked(int c, int r) const;      // collision incl. player tile
    void tickWaits();
    bool stepActorCommand(Actor& a);             // false = command was instant
    void applyCommandTo(Actor& a, int cmd);      // shared command interpreter
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
    std::vector<int> sentences_;                 // op 0x01 ScriptSentence: accumulated full-screen narration lines
    bool sentenceActive_ = false;
    bool choiceActive_ = false;                  // 0x3c pause: pick then resume VM
    VMChoice choice_;
    int choiceSel_ = 0;
    const Event* pendingEv_ = nullptr;           // event awaiting choice resume
    VMEncounter pendingEnc_;                     // 0x50 battle awaiting start/resume
    bool encLaunched_ = false;
    std::vector<const Event*> autoQueue_;        // pending auto-run events
    std::vector<int> runCount_;                  // per-event auto-run count (loop guard)
    int autoBudget_ = 32;                        // total auto runs per map visit
    // cutscene state
    std::vector<Actor> actors_;
    int lookActorId_ = -2;                       // 0x1b camera target (-2 = player)
    std::vector<uint8_t> playerCmds_; size_t playerCmdIdx_ = 0;  // 0x68 on unknown id -> player
    int playerWait_ = 0;
    struct PendingWait { const Event* ev = nullptr; int block = -1;
                         int ticks = -1; bool actors = false;
                         bool valid() const { return ev && block >= 0; } };
    PendingWait wait_;
    // 0x66 call frames suspended by a pause (innermost resume first, then these)
    std::vector<std::pair<const Event*, int>> resumeStack_;
    bool scriptPaused() const {
        return wait_.valid() || choiceActive_ || pendingEv_ != nullptr || pendingEnc_.valid();
    }
    void continueChain();
};

}  // namespace ffsmith
