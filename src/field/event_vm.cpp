#include "field/event_vm.h"
#include <cstdio>

namespace ffsmith {

// Door / map-edge warps are encoded as two cooperating opcodes (decoded from
// FieldClass::MoveScript case 0x6b + the real game data):
//   0x6B "BulkSetVars": sub=b[1], count=b[2], then count*(key BE16, val BE32).
//        sub==2 writes the script-variable bank (engine this+0xe9ac).  A warp sets
//        var0 = dest map, var2 = dest x, var3 = dest y, var4 = facing dir.
//   0x66 "SetEntityAction": action byte b[4]==0x04 = "move map" (consumes those
//        vars); b[4]==0x03 = spawn an NPC/object (NOT a warp).
// So a warp = an event whose script sets var0>0 via 0x6B sub2 AND issues 0x66 action 04.
VMOut run_event(const Event& ev) {
    VMOut o;
    char tmp[80];
    long wv0 = -1, wv2 = 0, wv3 = 0, wv4 = 0;   // script vars 0,2,3,4 (warp map/x/y/dir)
    bool warpAction = false;

    for (const auto& b : ev.scripts) {
        if (b.empty()) continue;
        const int op = b[0];
        if (op == 0x57) { o.log.push_back("ScriptEnd"); o.sawEnd = true; break; }
        switch (op) {
            case 0x00: {  // SetMessage: msg id = first operand word, big-endian (matches toolkit)
                int id = (b.size() > 2) ? ((b[1] << 8) | b[2])
                       : (b.size() > 1) ? b[1] : -1;
                o.messages.push_back(id);
                std::snprintf(tmp, sizeof(tmp), "SetMessage msg=%d", id);
                o.log.push_back(tmp);
                break;
            }
            case 0x41: {  // MapChange (direct warp): flag, map(BE word), x, y, dir, sub
                if (b.size() >= 7) {
                    o.warpMap = (b[2] << 8) | b[3];
                    o.warpX = b[4]; o.warpY = b[5]; o.warpDir = b[6];
                } else if (b.size() > 3) {
                    o.warpMap = (b[2] << 8) | b[3];
                }
                std::snprintf(tmp, sizeof(tmp), "MapChange -> map %d @(%d,%d) dir %d",
                              o.warpMap, o.warpX, o.warpY, o.warpDir);
                o.log.push_back(tmp);
                break;
            }
            case 0x6b: {  // BulkSetVars; sub==2 = script-variable bank
                if (b.size() >= 3) {
                    int sub = b[1], cnt = b[2];
                    size_t p = 3;
                    for (int k = 0; k < cnt && p + 6 <= b.size(); ++k, p += 6) {
                        int key = (b[p] << 8) | b[p + 1];
                        long val = ((long)b[p + 2] << 24) | ((long)b[p + 3] << 16)
                                 | (b[p + 4] << 8) | b[p + 5];
                        if (sub == 2) {
                            if (key == 0) wv0 = val; else if (key == 2) wv2 = val;
                            else if (key == 3) wv3 = val; else if (key == 4) wv4 = val;
                        }
                    }
                    std::snprintf(tmp, sizeof(tmp), "BulkSetVars sub=%d n=%d", sub, cnt);
                    o.log.push_back(tmp);
                }
                break;
            }
            case 0x66:    // SetEntityAction; action 0x04 = move-map (warp), 0x03 = spawn
                if (b.size() > 4 && b[4] == 0x04) warpAction = true;
                o.log.push_back("SetEntityAction");
                break;
            case 0x03: o.log.push_back("SetReferenceVariable"); break;
            case 0x04: o.log.push_back("SetReferenceFlag"); break;
            case 0x35: o.log.push_back("PlayBGM"); break;
            case 0x3d: o.log.push_back("ScriptIf (linear: not branched)"); break;
            default: {
                std::snprintf(tmp, sizeof(tmp), "op 0x%02x", op);
                o.log.push_back(tmp);
                break;
            }
        }
    }
    // Resolve a BulkSetVars+SetEntityAction(04) warp (unless a direct 0x41 already set one).
    if (o.warpMap < 0 && warpAction && wv0 > 0) {
        o.warpMap = (int)wv0; o.warpX = (int)wv2; o.warpY = (int)wv3; o.warpDir = (int)wv4;
        std::snprintf(tmp, sizeof(tmp), "Warp(vars) -> map %d @(%d,%d) dir %d",
                      o.warpMap, o.warpX, o.warpY, o.warpDir);
        o.log.push_back(tmp);
    }
    return o;
}

}  // namespace ffsmith
