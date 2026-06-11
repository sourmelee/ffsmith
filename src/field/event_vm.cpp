#include "field/event_vm.h"
#include <cstdio>

namespace ffsmith {

// Big-endian operand readers (GetBuffToWord / GetBuffToLong).
static int  rdW(const std::vector<uint8_t>& b, size_t i) {
    return (i + 1 < b.size()) ? ((b[i] << 8) | b[i + 1]) : 0;
}
static long rdL(const std::vector<uint8_t>& b, size_t i) {
    return (i + 3 < b.size())
        ? (((long)b[i] << 24) | ((long)b[i+1] << 16) | (b[i+2] << 8) | b[i+3]) : 0;
}
// Operand with script-var indirection (ReadScriptVariableParamsOfShort).
static long ind(const ScriptState& st, long raw, int mask, int bit) {
    return ((mask >> bit) & 1) ? st.getVar(2, (int)raw) : raw;
}

static void run_event_depth(const Event& ev, ScriptState& st, const VMEnv& env,
                            int startBlock, int depth, VMOut& o);

VMOut run_event(const Event& ev, ScriptState& st, const VMEnv& env, int startBlock) {
    VMOut o;
    run_event_depth(ev, st, env, startBlock, 0, o);
    return o;
}

static void run_event_depth(const Event& ev, ScriptState& st, const VMEnv& env,
                            int startBlock, int depth, VMOut& o) {
    char tmp[120];
    const int n = (int)ev.scripts.size();
    int pc = startBlock;
    int steps = 0;

    while (pc >= 0 && pc < n && ++steps < 2048) {
        const auto& b = ev.scripts[pc];
        int next = pc + 1;                                   // registry mode 3 (default)
        if (b.empty()) { pc = next; continue; }
        const int op = b[0];
        switch (op) {
            case 0x00:                                       // SetMessage
            case 0x01: {                                     // ScriptSentence (cinematic text)
                int id = rdW(b, 1);
                o.messages.push_back(id);
                std::snprintf(tmp, sizeof(tmp), "blk%d: %s msg=%d", pc,
                              op == 0x00 ? "SetMessage" : "ScriptSentence", id);
                o.log.push_back(tmp);
                break;
            }
            case 0x32: {                                     // wait N frames (placeholder)
                std::snprintf(tmp, sizeof(tmp), "blk%d: Wait %d (skipped)", pc, rdW(b, 1));
                o.log.push_back(tmp);
                break;
            }
            case 0x03: {                                     // SetReferenceVariable
                // refTarget=b[1], mask=b[2]; varType=w@3, varIdx=w@5, calcOp=w@7,
                // refP2=w@9, refType=w@0xb, refIdx=L@0xd  (MoveScript case 3)
                int mask = (b.size() > 2) ? b[2] : 0;
                int vt = (int)ind(st, rdW(b, 3), mask, 0);
                int vi = (int)ind(st, rdW(b, 5), mask, 1);
                int co = (int)ind(st, rdW(b, 7), mask, 2);
                int rp = (int)ind(st, rdW(b, 9), mask, 3);
                int rt = (int)ind(st, rdW(b, 0xb), mask, 4);
                long ri = ind(st, rdL(b, 0xd), mask, 5);
                long cur = st.getVar(vt, vi);
                long ref = get_reference(st, env, b.size() > 1 ? b[1] : 0, rp, rt, ri);
                long v = 0;
                switch (co) {
                    case 0: v = ref; break;
                    case 1: v = cur + ref; break;
                    case 2: v = cur - ref; break;
                    case 3: v = cur * ref; break;
                    case 4: v = ref ? cur / ref : 0; break;
                    case 5: v = ref ? cur % ref : 0; break;
                    case 6: v = cur & ref; break;
                    case 7: v = cur | ref; break;
                    case 8: v = cur ^ ref; break;
                    default: v = 0; break;
                }
                st.setVar(vt, vi, (int32_t)v);
                std::snprintf(tmp, sizeof(tmp), "blk%d: var[%d][%d] = %ld (op%d ref t%d=%ld)",
                              pc, vt, vi, v, co, b.size() > 1 ? b[1] : 0, ref);
                o.log.push_back(tmp);
                break;
            }
            case 0x04: {                                     // SetReferenceFlag
                int mask = (b.size() > 1) ? b[1] : 0;
                int ft = (int)ind(st, rdW(b, 2), mask, 0);
                int fi = (int)ind(st, rdW(b, 4), mask, 1);
                int ct = (int)ind(st, rdW(b, 6), mask, 2);
                st.setFlag(ft, fi, ct);
                std::snprintf(tmp, sizeof(tmp), "blk%d: flag[%d][%d] %s", pc, ft, fi,
                              ct == 0 ? "clear" : ct == 1 ? "set" : "toggle");
                o.log.push_back(tmp);
                break;
            }
            case 0x06: {                                     // timer set (this+4)
                int mask = (b.size() > 4) ? b[4] : 0;
                long a0 = ind(st, rdW(b, 5), mask, 0);
                long a1 = ind(st, rdW(b, 7), mask, 1);
                st.timer = (int32_t)(a1 * 0xf + a0 * 900);
                std::snprintf(tmp, sizeof(tmp), "blk%d: timer=%d", pc, st.timer);
                o.log.push_back(tmp);
                break;
            }
            case 0x35: case 0x36: case 0x75: {               // PlayBGM family
                o.bgm = rdW(b, 2);
                std::snprintf(tmp, sizeof(tmp), "blk%d: PlayBGM track=%d", pc, o.bgm);
                o.log.push_back(tmp);
                break;
            }
            case 0x3c: {                                     // MultiChoiceDialog
                int cnt = (b.size() > 2) ? b[2] : 0;
                o.choice.options.clear();
                for (int k = 0; k < cnt && (size_t)(6 + 4 * k) <= b.size(); ++k)
                    o.choice.options.emplace_back(rdW(b, 3 + 4 * k), rdW(b, 5 + 4 * k));
                o.choice.defaultBlock = pc + 1;
                o.choiceEv = &ev;
                o.hasChoice = true;
                std::snprintf(tmp, sizeof(tmp), "blk%d: choice (%d options)", pc, cnt);
                o.log.push_back(tmp);
                return;                                      // pause for selection
            }
            case 0x3d: {                                     // ScriptIf (jump on FAIL)
                int mask = (b.size() > 1) ? b[1] : 0;
                long lv = get_reference(st, env,
                            (b.size() > 3) ? b[3] : 0,
                            (int)ind(st, rdW(b, 4), mask, 0),
                            (int)ind(st, rdW(b, 6), mask, 1),
                            ind(st, rdL(b, 8), mask, 2));
                long rv = get_reference(st, env,
                            (b.size() > 0xd) ? b[0xd] : 0,
                            (int)ind(st, rdW(b, 0xe), mask, 3),
                            (int)ind(st, rdW(b, 0x10), mask, 4),
                            ind(st, rdL(b, 0x12), mask, 5));
                int cop = (int)ind(st, rdW(b, 0x16), mask, 6);
                int tgt = rdW(b, 0x18);
                bool hold = check_condition(lv, cop, rv);
                if (!hold) next = tgt;                       // SetRegistryNegative path
                std::snprintf(tmp, sizeof(tmp), "blk%d: if %ld op%d %ld -> %s",
                              pc, lv, cop, rv, hold ? "fallthrough" : "jump");
                o.log.push_back(tmp);
                break;
            }
            case 0x3f:                                       // Jump
                next = rdW(b, 1);
                std::snprintf(tmp, sizeof(tmp), "blk%d: jump -> blk%d", pc, next);
                o.log.push_back(tmp);
                break;
            case 0x40: {                                     // RandomJump
                int cnt = (b.size() > 1) ? b[1] : 0;
                int pick = (cnt > 0 && env.rand) ? env.rand(cnt) : 0;
                next = rdW(b, 2 + 2 * pick);
                std::snprintf(tmp, sizeof(tmp), "blk%d: random jump -> blk%d", pc, next);
                o.log.push_back(tmp);
                break;
            }
            case 0x41: {                  // MapChange: map, layer, x, y, dir (SetMapChangeSelect)
                int mask = (b.size() > 1) ? b[1] : 0;
                o.warpMap = (int)ind(st, rdW(b, 2), mask, 0);
                o.warpX   = (int)ind(st, rdW(b, 6), mask, 2);
                o.warpY   = (int)ind(st, rdW(b, 8), mask, 3);
                o.warpDir = (int)ind(st, rdW(b, 10), mask, 4);
                std::snprintf(tmp, sizeof(tmp), "blk%d: MapChange -> map %d @(%d,%d) dir %d",
                              pc, o.warpMap, o.warpX, o.warpY, o.warpDir);
                o.log.push_back(tmp);
                break;
            }
            case 0x50: {                  // ScriptEncount (FieldClass::ScriptEncount c:120371)
                // Seven (flag u8, BE u16) pairs; flag != 0 resolves the word
                // through GetReferenceIndex (script-var indirection, bank 2 —
                // same idiom as 0x41's mask).
                auto refIdx = [&](size_t fo, size_t wo) -> int {
                    int w = rdW(b, wo);
                    w = (w ^ 0x8000) - 0x8000;               // GetBuffToWord = signed short
                    return (fo < b.size() && b[fo]) ? (int)st.getVar(2, w) : w;
                };
                o.enc.formation = refIdx(1, 2);
                o.enc.bgId      = refIdx(4, 5);
                o.enc.bgVar     = refIdx(7, 8);
                o.enc.condition = refIdx(10, 0xb);
                o.enc.bgm       = refIdx(0xd, 0xe);
                o.enc.bgmCmp    = refIdx(0x10, 0x11);
                o.enc.flags     = refIdx(0x13, 0x14);
                o.enc.resumeBlock = pc + 1;
                o.enc.ev = &ev;
                o.hasEncounter = true;
                std::snprintf(tmp, sizeof(tmp),
                              "blk%d: ScriptEncount formation=%d cond=%d bgm=%d",
                              pc, o.enc.formation, o.enc.condition, o.enc.bgm);
                o.log.push_back(tmp);
                return;                                      // pause for the battle
            }
            case 0x57:                                       // ScriptEnd (registry mode 4)
                o.sawEnd = true;
                o.log.push_back("ScriptEnd");
                pc = n;
                continue;
            case 0x66: {                                     // CallEvent(id, startBlock)
                int mask = (b.size() > 2) ? b[2] : 0;
                int id   = (int)ind(st, rdW(b, 3), mask, 0);
                int line = (int)ind(st, rdW(b, 5), mask, 1);
                const Event* callee = env.findEvent ? env.findEvent(id) : nullptr;
                std::snprintf(tmp, sizeof(tmp), "blk%d: CallEvent 0x%x blk%d %s",
                              pc, id, line, callee ? "" : "(not found)");
                o.log.push_back(tmp);
                if (callee && depth < 6)
                    run_event_depth(*callee, st, env, line, depth + 1, o);
                else if (callee)
                    o.log.push_back("CallEvent: depth cap hit");
                if (o.hasChoice || o.hasEncounter) return;   // callee paused (choice/battle)
                break;
            }
            case 0x6b: {                                     // BulkSetVars
                if (b.size() >= 3) {
                    int sub = b[1], cnt = b[2];
                    size_t p = 3;
                    for (int k = 0; k < cnt && p + 6 <= b.size(); ++k, p += 6) {
                        int key = (b[p] << 8) | b[p + 1];
                        long val = ((long)b[p+2] << 24) | ((long)b[p+3] << 16)
                                 | (b[p+4] << 8) | b[p+5];
                        if (sub == 2) st.setVar(2, key, (int32_t)val);
                    }
                    std::snprintf(tmp, sizeof(tmp), "blk%d: BulkSetVars sub=%d n=%d", pc, sub, cnt);
                    o.log.push_back(tmp);
                }
                break;
            }
            default: {
                std::snprintf(tmp, sizeof(tmp), "blk%d: op 0x%02x", pc, op);
                o.log.push_back(tmp);
                break;
            }
        }
        pc = next;
    }
    (void)tmp;
}

}  // namespace ffsmith
