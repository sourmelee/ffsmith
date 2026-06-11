#pragma once
#include <string>
#include <utility>
#include <vector>
#include "data/bundle.h"
#include "field/script_state.h"

namespace ffsmith {

// One dialogue-choice request (op 0x3c MultiChoiceDialog / StartMessageSelect):
// pick option k -> resume at options[k].second; cancel -> defaultBlock.
struct VMChoice {
    std::vector<std::pair<int, int>> options;   // (value [msg id], target block)
    int defaultBlock = 0;
};

// One scripted-battle request (op 0x50 ScriptEncount, FieldClass::ScriptEncount
// c:120371): seven (indirect-flag u8, BE u16) operand pairs.
struct VMEncounter {
    int formation = -1;   // params[0] -> BattleClass::LoadFormation
    int bgId = -1, bgVar = 0;        // params[1..2] -> "btlbg%d_%d.dat"
    int condition = 0;    // params[3] 1..3 -> battle-condition flag table
    int bgm = -1, bgmCmp = -1;       // params[4..5]; p4==p5 -> "keep BGM" flag
    int flags = 0;        // params[6] bit0 clear -> flag 0x10; bit1 -> result-flag 2
    int resumeBlock = -1; // continue the script here when the battle ends
    const Event* ev = nullptr;       // event owning the paused script
    bool valid() const { return formation >= 0; }
};

// One 0x68 StartEntityAction: command bytes for an actor (FieldClass command
// table DAT_00418d40, extracted from libjniproxy.so — see formats/events.md).
struct VMActorAction { int id = -1; int mode = 0; std::vector<uint8_t> cmds; };
// One 0x20 TeleportNPC (absent fields = -1 -> keep current).
struct VMTeleport { int id = -1, x = -1, y = -1, dir = -1; };

struct VMOut {
    std::vector<int> messages;       // SetMessage ids, in execution order
    std::vector<std::string> log;    // human-readable action trace
    bool sawEnd = false;
    int  warpMap = -1, warpX = 0, warpY = 0, warpDir = -1;
    bool hasChoice = false;          // stopped at a 0x3c — resume via run_event(start)
    const Event* choiceEv = nullptr; // event owning the choice (may be a 0x66 callee)
    VMChoice choice;
    int  bgm = -1;                   // PlayBGM track (0x35), if any
    bool hasEncounter = false;       // stopped at a 0x50 — battle, then resume
    VMEncounter enc;
    // --- cutscene side effects (applied by Field in order) ---
    std::vector<VMActorAction> actions;          // 0x68
    std::vector<VMTeleport> teleports;           // 0x20
    std::vector<std::pair<int,int>> visibles;    // 0x21: (id, on)
    int cameraTarget = -2;                       // 0x1b: entity id (-2 = none)
    int playerX = -1, playerY = -1, playerDir = -1;
    bool hasPlayerSet = false;                   // 0x55
    int fadeMode = -1, fadeR = 0, fadeG = 0, fadeB = 0, fadeTicks = 0;  // 0x2a
    bool fadeWait = false;
    // --- timed pauses (resume via run_event(ev, st, env, pauseBlock)) ---
    int  waitTicks = -1;             // 0x32: pause N ticks
    bool waitActors = false;         // 0x69: pause until actors idle
    int  pauseBlock = -1;            // block to resume at
    const Event* pauseEv = nullptr;
    // When a pause happens inside a 0x66 callee, the caller frames that must
    // continue after the innermost resume finishes (outermost last).
    std::vector<std::pair<const Event*, int>> resumeStack;
};

// Execute an event's script blocks from `startBlock` (FieldClass::MoveEventScript
// block runner + MoveScript dispatch).  Branching: ScriptIf 0x3d jumps to its
// target block when the condition FAILS; 0x3f/0x40 jump; 0x57 ends; default is
// fall-through to the next block.  Flags/vars live in `st`; external lookups
// (items / party / RNG) come from `env`.
VMOut run_event(const Event& ev, ScriptState& st, const VMEnv& env, int startBlock = 0);

}  // namespace ffsmith
