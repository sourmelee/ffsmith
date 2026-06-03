#pragma once
#include <string>
#include <vector>
#include "data/bundle.h"

namespace ffsmith {

// Result of running one event's script blocks (first-cut linear interpreter).
struct VMOut {
    std::vector<int> messages;       // SetMessage ids, in encounter order
    std::vector<std::string> log;    // human-readable action trace
    bool sawEnd = false;
    int  warpMap = -1, warpX = 0, warpY = 0, warpDir = -1;  // MapChange dest (else map=-1)
};

// Execute an event's scripts. First cut: linear walk of length-split blocks,
// dispatching on block[0]; surfaces dialogue (0x00 SetMessage), records flags/
// vars/warps, stops at 0x57 ScriptEnd. Conditionals/jumps are a refinement.
VMOut run_event(const Event& ev);

}  // namespace ffsmith
