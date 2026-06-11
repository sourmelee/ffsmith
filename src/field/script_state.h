#pragma once
// Script flags / variables / appear-conditions — the FieldClass state banks.
// RE'd 2026-06-10 from libjniproxy.so_new.c:
//   flags: FieldClass::SetReferenceFlag / IsFlagEnabled (c:134634) — banks
//     0 @+0xe478 (128b)  1 @+0xe494+page*0x40 (512b/page)  2 @+0xe488 (96b)
//     3 @+0xe614+page*0x40  4 @+0xe794+page*4 (32b/page)  5 = GameClass+0x1a174 (32b global)
//   vars: GetVariable / SetVariable (c:133737) — banks
//     0 @+0xe7ac (128)  2 @+0xe9ac (512, the script-var bank)  3 @+0xf1ac (24)
//     1 = alias into bank 2 at [0x20 + page*0x50]  4 @+0xf20c+page*0x20 (8/page)
//     5 = system specials (0=msg bank, 2=page, 3=story state, ...)
//   page index = FieldClass+0xe474.
//   references: FieldClass::GetReference (c:134452); conditions: CheckCondition.
//   appear: FieldClass::CheckEventAppear (c:137096; FFV twin c:168498 is the
//     readable one): 6 slots — flag, flag, variable, item, member, timer.
#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>

namespace ffsmith {

struct Event;  // data/bundle.h

struct VMEnv {
    std::function<int(int)>  itemCount;  // owned count of item id (GetReferenceItem)
    std::function<bool(int)> partyHas;   // member id in party (GetReferenceParty)
    std::function<int(int)>  rand;       // uniform 0..n-1
    std::function<const Event*(int)> findEvent;  // CallEvent 0x66: map events + common pool (map 10000)
    // GetReference target 8 -> GetReferenceBattle (c:135841): type 3 = last
    // battle result (1 = won [flag bit9 clear], 2 = escaped [bit10]).
    std::function<long(int, long)> battleRef;    // (type, idx) -> value
};

struct ScriptState {
    static constexpr int PAGES = 8;      // real engine uses 6 page slots; headroom
    // flag banks (bit-addressed u32 words)
    uint32_t f0[4] = {};                 // type 0: 128 flags
    uint32_t f2[3] = {};                 // type 2: 96 flags
    uint32_t f5    = 0;                  // type 5: 32 global flags (GameClass+0x1a174)
    uint32_t f1[PAGES][16] = {};         // type 1: 512 flags / page
    uint32_t f3[PAGES][16] = {};         // type 3: 512 flags / page
    uint32_t f4[PAGES] = {};             // type 4: 32 flags / page
    // variable banks (i32)
    int32_t v0[128] = {};
    int32_t v2[512] = {};                // the script-var bank (this+0xe9ac)
    int32_t v3[24]  = {};
    int32_t v4[PAGES][8] = {};
    // system specials (var bank 5)
    int32_t page = 0;                    // idx 2 (this+0xe474)
    int32_t msgBank = 0;                 // idx 0 (GameClass+0x19fe0)
    int32_t storyState = 0;              // idx 3 (this+0xe464)
    int32_t sys5 = 0, sys7 = 0;          // idx 5 (+0xe468), idx 7 (+0xe46c)
    bool    sys6 = false;                // idx 6 (byte +0xdc36)
    int32_t timer = 0;                   // this+4 (script timer; appear slot 5)
    bool dirty = false;                  // a flag/var was written (appear re-eval)

    int     getFlag(int type, int idx) const;
    void    setFlag(int type, int idx, int control);   // 0=clear 1=set 2=toggle
    int32_t getVar(int type, int idx) const;
    void    setVar(int type, int idx, int32_t v);

    std::vector<uint8_t> serialize() const;            // fixed-size blob (save)
    bool deserialize(const uint8_t* p, size_t n);
};

// FieldClass::GetReference — resolve a (target, p2, type, index) reference.
long get_reference(const ScriptState& st, const VMEnv& env,
                   int target, int p2, int type, long idx);
// FieldClass::CheckCondition — op: 0:== 1/8:!= 2:> 3:< 4:>= 5:<= 6:& 7:either!=0
bool check_condition(long left, int op, long right);
// FieldClass::CheckEventAppear — 31-byte appear block (event header[9..0x27]).
bool check_event_appear(const std::vector<uint8_t>& appear,
                        const ScriptState& st, const VMEnv& env);

}  // namespace ffsmith
