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

struct VMOut {
    std::vector<int> messages;       // SetMessage ids, in execution order
    std::vector<std::string> log;    // human-readable action trace
    bool sawEnd = false;
    int  warpMap = -1, warpX = 0, warpY = 0, warpDir = -1;
    bool hasChoice = false;          // stopped at a 0x3c — resume via run_event(start)
    VMChoice choice;
    int  bgm = -1;                   // PlayBGM track (0x35), if any
};

// Execute an event's script blocks from `startBlock` (FieldClass::MoveEventScript
// block runner + MoveScript dispatch).  Branching: ScriptIf 0x3d jumps to its
// target block when the condition FAILS; 0x3f/0x40 jump; 0x57 ends; default is
// fall-through to the next block.  Flags/vars live in `st`; external lookups
// (items / party / RNG) come from `env`.
VMOut run_event(const Event& ev, ScriptState& st, const VMEnv& env, int startBlock = 0);

}  // namespace ffsmith
