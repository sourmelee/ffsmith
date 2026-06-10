#include "field/script_state.h"
#include <cstdio>
#include <cstring>

namespace ffsmith {

// --- flag banks -------------------------------------------------------------
// Word/bit addressing mirrors the C: word = idx>>5, bit = idx&0x1f.
static uint32_t* flag_word(ScriptState& s, int type, int idx, int& bit) {
    if (idx < 0) return nullptr;
    bit = idx & 0x1f;
    int w = idx >> 5;
    int pg = (s.page >= 0 && s.page < ScriptState::PAGES) ? s.page : 0;
    switch (type) {
        case 0: return (w < 4)  ? &s.f0[w]      : nullptr;
        case 1: return (w < 16) ? &s.f1[pg][w]  : nullptr;
        case 2: return (w < 3)  ? &s.f2[w]      : nullptr;
        case 3: return (w < 16) ? &s.f3[pg][w]  : nullptr;
        case 4: return (w < 1)  ? &s.f4[pg]     : nullptr;
        case 5: return (w < 1)  ? &s.f5         : nullptr;
        default: return nullptr;
    }
}

int ScriptState::getFlag(int type, int idx) const {
    int bit = 0;
    uint32_t* w = flag_word(const_cast<ScriptState&>(*this), type, idx, bit);
    if (!w) return 0;
    return (*w >> bit) & 1;
}

void ScriptState::setFlag(int type, int idx, int control) {
    int bit = 0;
    uint32_t* w = flag_word(*this, type, idx, bit);
    if (!w) return;
    if (control == 0)      *w &= ~(1u << bit);
    else if (control == 1) *w |=  (1u << bit);
    else if (control == 2) *w ^=  (1u << bit);
    dirty = true;
}

// --- variable banks ----------------------------------------------------------
int32_t ScriptState::getVar(int type, int idx) const {
    int pg = (page >= 0 && page < PAGES) ? page : 0;
    switch (type) {
        case 0: return (idx >= 0 && idx < 128) ? v0[idx] : 0;
        case 1: {                                  // alias into v2 (real: +0xea2c)
            int i = 0x20 + pg * 0x50 + idx;
            return (idx >= 0 && i < 512) ? v2[i] : 0;
        }
        case 2: return (idx >= 0 && idx < 512) ? v2[idx] : 0;
        case 3: return (idx >= 0 && idx < 24)  ? v3[idx] : 0;
        case 4: return (idx >= 0 && idx < 8)   ? v4[pg][idx] : 0;
        case 5:
            switch (idx) {                         // GetVariable type-5 specials
                case 0: return msgBank;
                case 2: return page;
                case 3: return storyState;
                case 4: return 3;                  // main mode: field
                case 5: return sys5;
                case 6: return sys6 ? 1 : 0;
                case 7: return sys7;
                default: return 1;                 // case 8 path returns 1
            }
        default: return 0;
    }
}

void ScriptState::setVar(int type, int idx, int32_t v) {
    int pg = (page >= 0 && page < PAGES) ? page : 0;
    switch (type) {
        case 0: if (idx >= 0 && idx < 128) v0[idx] = v; break;
        case 1: { int i = 0x20 + pg * 0x50 + idx; if (idx >= 0 && i < 512) v2[i] = v; break; }
        case 2: if (idx >= 0 && idx < 512) v2[idx] = v; break;
        case 3: if (idx >= 0 && idx < 24)  v3[idx] = v; break;
        case 4: if (idx >= 0 && idx < 8)   v4[pg][idx] = v; break;
        case 5:
            switch (idx) {
                case 0: msgBank = v; break;
                case 2: page = (v < 0xff && v >= 0) ? v : 0; break;
                case 3: storyState = v; break;
                case 5: sys5 = v; break;
                case 6: sys6 = (v != 0); break;
                case 7: sys7 = v; break;
                default: break;
            }
            break;
        default: return;
    }
    dirty = true;
}

// --- save blob ----------------------------------------------------------------
static void put32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8));
    b.push_back((uint8_t)(v >> 16)); b.push_back((uint8_t)(v >> 24));
}
static uint32_t take32(const uint8_t*& p) {
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    p += 4; return v;
}

std::vector<uint8_t> ScriptState::serialize() const {
    std::vector<uint8_t> b;
    b.push_back('S'); b.push_back('S'); b.push_back('T'); b.push_back(1);  // tag + version
    for (uint32_t w : f0) put32(b, w);
    for (uint32_t w : f2) put32(b, w);
    put32(b, f5);
    for (int p = 0; p < PAGES; ++p) for (uint32_t w : f1[p]) put32(b, w);
    for (int p = 0; p < PAGES; ++p) for (uint32_t w : f3[p]) put32(b, w);
    for (uint32_t w : f4) put32(b, w);
    for (int32_t v : v0) put32(b, (uint32_t)v);
    for (int32_t v : v2) put32(b, (uint32_t)v);
    for (int32_t v : v3) put32(b, (uint32_t)v);
    for (int p = 0; p < PAGES; ++p) for (int32_t v : v4[p]) put32(b, (uint32_t)v);
    put32(b, (uint32_t)page); put32(b, (uint32_t)msgBank); put32(b, (uint32_t)storyState);
    put32(b, (uint32_t)sys5); put32(b, (uint32_t)sys7); put32(b, sys6 ? 1u : 0u);
    put32(b, (uint32_t)timer);
    return b;
}

bool ScriptState::deserialize(const uint8_t* p, size_t n) {
    std::vector<uint8_t> golden = serialize();
    if (n != golden.size() || !p || std::memcmp(p, "SST\x01", 4) != 0) return false;
    p += 4;
    for (auto& w : f0) w = take32(p);
    for (auto& w : f2) w = take32(p);
    f5 = take32(p);
    for (int g = 0; g < PAGES; ++g) for (auto& w : f1[g]) w = take32(p);
    for (int g = 0; g < PAGES; ++g) for (auto& w : f3[g]) w = take32(p);
    for (auto& w : f4) w = take32(p);
    for (auto& v : v0) v = (int32_t)take32(p);
    for (auto& v : v2) v = (int32_t)take32(p);
    for (auto& v : v3) v = (int32_t)take32(p);
    for (int g = 0; g < PAGES; ++g) for (auto& v : v4[g]) v = (int32_t)take32(p);
    page = (int32_t)take32(p); msgBank = (int32_t)take32(p); storyState = (int32_t)take32(p);
    sys5 = (int32_t)take32(p); sys7 = (int32_t)take32(p); sys6 = take32(p) != 0;
    timer = (int32_t)take32(p);
    dirty = true;
    return true;
}

// --- references / conditions ----------------------------------------------------
bool check_condition(long l, int op, long r) {
    switch (op) {
        case 0: return l == r;
        case 1: case 8: return l != r;
        case 2: return l > r;
        case 3: return l < r;
        case 4: return l >= r;
        case 5: return l <= r;
        case 6: return (l & r) != 0;
        case 7: return l != 0 || r != 0;
        default: return false;
    }
}

long get_reference(const ScriptState& st, const VMEnv& env,
                   int target, int p2, int type, long idx) {
    switch (target) {
        case 1:  return st.getFlag(type, (int)idx);
        case 2:  return idx != 0 ? 1 : 0;
        case 3:  return st.getVar(type, (int)idx);
        case 5:  return (env.partyHas && env.partyHas((int)idx)) ? 1 : 0;
        case 7:  return env.itemCount ? env.itemCount((int)idx) : 0;
        case 9: case 10: return idx;                       // immediate constant
        case 0xf: {                                        // random in [type, idx)
            long span = (idx - type) * 10;
            if (span <= 0) return type;
            int r = env.rand ? env.rand((int)span) : 0;
            return r / 10 + type;
        }
        case 0x10: return type | ((long)p2 << 0x14) | (idx << 8);
        default:                                           // chara/event/etc: not yet
            std::printf("[FFSmith] GetReference target %d (p2=%d type=%d idx=%ld) unhandled -> 0\n",
                        target, p2, type, idx);
            return 0;
    }
}

// --- appear conditions -----------------------------------------------------------
// 6 slots in the 31-byte block: starts {0,5,10,19,22,25}, present when [start]!=0.
bool check_event_appear(const std::vector<uint8_t>& a,
                        const ScriptState& st, const VMEnv& env) {
    if (a.size() < 31) return true;
    auto w16 = [&](int i) { return (a[i] << 8) | a[i + 1]; };
    auto w32 = [&](int i) { return ((long)a[i] << 24) | ((long)a[i+1] << 16) | (a[i+2] << 8) | a[i+3]; };
    for (int slot = 0; slot < 6; ++slot) {
        static const int START[6] = {0, 5, 10, 19, 22, 25};
        const int s = START[slot];
        if (!a[s]) continue;
        bool ok = true;
        switch (slot) {
            case 0: case 1: {                       // flag: type, bit BE16, expect
                int set = st.getFlag(a[s + 1], w16(s + 2));
                ok = ((a[s + 4] != 0) == (set != 0));
                break;
            }
            case 2:                                 // variable: type, idx, value BE32, op
                ok = check_condition(st.getVar(a[s + 1], w16(s + 2)), a[s + 8], w32(s + 4));
                break;
            case 3:                                 // item owned
                ok = env.itemCount && env.itemCount(w16(s + 1)) > 0;
                break;
            case 4:                                 // member in party
                ok = env.partyHas && env.partyHas(w16(s + 1));
                break;
            case 5: {                               // timer window vs st.timer
                long v = w32(s + 1); int op = a[s + 5]; long t = st.timer;
                switch (op) {
                    case 0: ok = (t >= v && t < v + 15); break;
                    case 1: ok = (t < v || t >= v + 15); break;
                    case 2: ok = (t >= v + 15); break;
                    case 3: ok = (t < v); break;
                    case 4: ok = (t >= v); break;
                    case 5: ok = (t <= v + 15); break;
                    default: ok = false; break;
                }
                break;
            }
        }
        if (!ok) return false;
    }
    return true;
}

}  // namespace ffsmith
