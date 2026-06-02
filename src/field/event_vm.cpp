#include "field/event_vm.h"
#include <cstdio>

namespace ffsmith {

VMOut run_event(const Event& ev) {
    VMOut o;
    char tmp[64];
    for (const auto& b : ev.scripts) {
        if (b.empty()) continue;
        const int op = b[0];
        switch (op) {
            case 0x00: {  // SetMessage: msg id in the first operand word
                int id = (b.size() > 2) ? ((b[1] << 8) | b[2])   // big-endian, matches toolkit
                       : (b.size() > 1) ? b[1] : -1;
                o.messages.push_back(id);
                std::snprintf(tmp, sizeof(tmp), "SetMessage msg=%d", id);
                o.log.push_back(tmp);
                break;
            }
            case 0x57:  // ScriptEnd
                o.log.push_back("ScriptEnd");
                o.sawEnd = true;
                return o;
            case 0x41: {  // MapChange (warp) — fmt B W ...: target map = BE word at b[2..3]
                if (b.size() > 3) o.warpMap = (b[2] << 8) | b[3];
                std::snprintf(tmp, sizeof(tmp), "MapChange -> map %d (M3b)", o.warpMap);
                o.log.push_back(tmp);
                break;
            }
            case 0x03: o.log.push_back("SetReferenceVariable"); break;
            case 0x04: o.log.push_back("SetReferenceFlag"); break;
            case 0x6b: o.log.push_back("BulkSetVars"); break;
            case 0x66: o.log.push_back("SetEntityAction"); break;
            case 0x35: o.log.push_back("PlayBGM"); break;
            case 0x3d: o.log.push_back("ScriptIf (linear: not branched)"); break;
            default: {
                std::snprintf(tmp, sizeof(tmp), "op 0x%02x", op);
                o.log.push_back(tmp);
                break;
            }
        }
    }
    return o;
}

}  // namespace ffsmith
