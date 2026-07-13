#define SDL_MAIN_HANDLED
#include "host/host.h"
#include "data/bundle.h"
#include "render/compositor.h"
#include "field/field.h"
#include "field/event_vm.h"

#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <array>
#include <vector>

using namespace ffsmith;

static int takeInt(int argc, char** argv, int& i, int fb) { if (i+1<argc) return std::atoi(argv[++i]); return fb; }
static const char* takeStr(int argc, char** argv, int& i, const char* fb) { if (i+1<argc) return argv[++i]; return fb; }
static int dirBtnFromChar(char ch) {
    switch (ch) { case 'U': case 'u': return BTN_UP; case 'D': case 'd': return BTN_DOWN;
                  case 'L': case 'l': return BTN_LEFT; case 'R': case 'r': return BTN_RIGHT; default: return 0; }
}
static const char* faceName(int f) { static const char* n[4]={"down","up","left","right"}; return (f>=0&&f<4)?n[f]:"?"; }
static int bankOf(const std::string& key) { int g = 0; std::sscanf(key.c_str(), "g%d", &g); return g; }  // map group = dialogue bank

static void printUsage(const char* exe) {
    std::printf(
        "FFSmith - clean-room engine for Final Fantasy Dimensions / Legends\n"
        "Usage: %s [options]\n"
        "  --map KEY      map to load, e.g. g0_p0_m501 (bundle defaults to exe folder)\n"
        "  --bundle DIR   asset bundle dir (has maps/ and tex/); default: exe folder\n"
        "  --player C,R   start tile (default: map centre)\n"
        "  --player-img N fldchr id for the player sprite (default: first NPC sprite)\n"
        "  --shot PATH    compose map, write .tex, exit (headless)\n"
        "  --fieldshot P  render one field frame to a .tex and exit (headless)\n"
        "  --walk SCRIPT  headless: feed U/D/L/R steps (follows warps), print path, exit\n"
        "  --events       headless: list events + dry-run their scripts + a talk sim, exit\n"
        "  --face N        set player facing for --fieldshot (0=D,1=U,2=L,3=R)\n"
        "  --frames N     run N ticks then exit\n"
        "  --scale N      zoom factor (default 3); resize the window to see more map\n"
        "  --aspect W:H   lock to an aspect ratio (4:3, 16:9, 9:16, ...); UI reflows to fill\n"
        "  --title        boot to the title screen (Start/Z -> field)\n"
        "  --menu         open the field menu (for screenshots)\n"
        "  --debug        boot into the debug launcher (default for windowed)\n"
        "  --noclip/--overlay/--hud   field debug toggles (for screenshots)\n"
        "  --encounters   enable random encounters (decoded areas, approx roll)\n"
        "  --hz N         logic tick rate (default 60)\n"
        "  --help         show this help\n"
        "Controls: arrows/WASD move, Z confirm, Enter/Tab menu, X cancel, Esc quit. Resizable.\n", exe);
}

static void dump_events(const FfMap& m, int tile, const FfMap* common = nullptr) {
    std::printf("[FFSmith] map events: %zu\n", m.events.size());
    ScriptState st;                       // throwaway state for the dump
    VMEnv env; env.rand = [](int n) { return n > 0 ? 0 : 0; };
    env.findEvent = [&](int id) -> const Event* {
        for (const auto& e : m.events) if (e.id == id) return &e;
        if (common) for (const auto& e : common->events) if (e.id == id) return &e;
        return nullptr;
    };
    for (size_t i = 0; i < m.events.size(); ++i) {
        const Event& e = m.events[i];
        std::printf("  ev[%zu] (%2d,%2d) type=%d boot=0x%02x img=%d/%d scripts=%zu appear=%d\n",
                    i, e.x, e.y, e.type, e.boot, e.img, e.var, e.scripts.size(),
                    (int)check_event_appear(e.appear, st, env));
        VMOut o = run_event(e, st, env);
        for (const auto& l : o.log) std::printf("       . %s\n", l.c_str());
        if (!o.messages.empty()) { std::printf("       => messages:"); for (int id : o.messages) std::printf(" %d", id); std::printf("\n"); }
    }
    const Event* npc = nullptr;
    for (const auto& e : m.events) if (e.img > 0 && !e.scripts.empty()) { npc = &e; break; }
    if (npc) {
        struct A { int dx, dy; uint32_t btn; } adj[4] = {{0,1,BTN_UP},{0,-1,BTN_DOWN},{1,0,BTN_LEFT},{-1,0,BTN_RIGHT}};
        for (auto& a : adj) {
            int sx = npc->x + a.dx, sy = npc->y + a.dy;
            if (sx < 0 || sy < 0 || sx >= m.w || sy >= m.h) continue;
            Field f(&m, tile, sx, sy);
            if (f.isSolid(sx, sy)) continue;
            InputState face; face.held = a.btn; f.update(face);
            InputState conf; conf.pressed = BTN_CONFIRM; f.update(conf);
            std::printf("[sim] stand (%d,%d) face %s -> NPC(%d,%d): inDialogue=%d msg=%d\n",
                        sx, sy, faceName(f.facing()), npc->x, npc->y, f.inDialogue(), f.dialogueMsg());
            break;
        }
    }
}


// --- VM self-test (no SDL): branching, flags/vars, appear, save blob ---------
static std::vector<uint8_t> hexblk(const char* hx) {
    std::vector<uint8_t> v;
    for (const char* p = hx; p[0] && p[1]; p += 2) {
        auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
        v.push_back((uint8_t)((nib(p[0]) << 4) | nib(p[1])));
    }
    return v;
}

static int vmSelfTest() {
    int fails = 0;
    auto expect = [&](bool ok, const char* what) {
        std::printf("[vmtest] %-44s %s\n", what, ok ? "PASS" : "FAIL");
        if (!ok) ++fails;
    };
    VMEnv env; env.rand = [](int n) { return n > 0 ? 0 : 0; };
    // Event A: set flag1[0], if flag1[0]==1 fallthrough -> msg 100, end; else -> msg 200
    Event ev;
    ev.scripts.push_back(hexblk("0400000100000001"));            // flag[1][0] set
    // ScriptIf: left=flag(type1,idx0) right=imm 1 op== -> target blk 4 on FAIL
    ev.scripts.push_back(hexblk("3d0000010000000100000000000a000000000000000100000004"));
    ev.scripts.push_back(hexblk("000064000000000000"));          // msg 100
    ev.scripts.push_back(hexblk("57"));                          // end
    ev.scripts.push_back(hexblk("0000c8000000000000"));          // msg 200 (FAIL branch)
    {
        ScriptState st;
        VMOut o = run_event(ev, st, env);
        expect(st.getFlag(1, 0) == 1, "0x04 sets flag bank1 bit0");
        expect(o.messages.size() == 1 && o.messages[0] == 100, "ScriptIf TRUE falls through (msg 100)");
        expect(o.sawEnd, "ScriptEnd reached");
    }
    {
        ScriptState st;                       // flag clear -> condition fails -> jump
        VMOut o = run_event(ev, st, env, 1);  // skip the flag-set block
        expect(o.messages.size() == 1 && o.messages[0] == 200, "ScriptIf FALSE jumps (msg 200)");
    }
    {
        ScriptState st;                       // 0x3f jump skips msg
        Event ej; ej.scripts.push_back(hexblk("3f0002"));
        ej.scripts.push_back(hexblk("000064000000000000"));
        ej.scripts.push_back(hexblk("57"));
        VMOut o = run_event(ej, st, env);
        expect(o.messages.empty() && o.sawEnd, "0x3f jumps over a block");
    }
    {
        ScriptState st;             // door idiom: 0x6b vars + 0x66 CallEvent 0x104 -> 0x41
        Event callee; callee.id = 0x104;     // minimal stand-in for common event 0x104
        callee.scripts.push_back(hexblk("411f00000001000200030004"));  // map,layer,x,y,dir = v0..v4
        callee.scripts.push_back(hexblk("57"));
        VMEnv env2 = env;
        env2.findEvent = [&](int id) -> const Event* { return id == 0x104 ? &callee : nullptr; };
        Event ew;
        ew.scripts.push_back(hexblk("6b0205" "0000" "000001f4" "0001" "00000000" "0002" "00000025" "0003" "0000001c" "0004" "00000001"));
        ew.scripts.push_back(hexblk("66010001040000"));
        ew.scripts.push_back(hexblk("57"));
        VMOut o = run_event(ew, st, env2);
        expect(o.warpMap == 500 && o.warpX == 0x25 && o.warpY == 0x1c && o.warpDir == 1,
               "0x6b + CallEvent 0x104 -> map 500 @(37,28)");
        expect(st.getVar(2, 0) == 500, "script var bank persists");
    }
    {
        ScriptState st;                       // SetReferenceVariable: storyState = imm 11
        Event ec; ec.scripts.push_back(hexblk("030900" "0005" "0003" "0000" "0000" "0007" "0000000b"));
        ec.scripts.push_back(hexblk("57"));
        run_event(ec, st, env);
        expect(st.storyState == 11, "0x03 var[5][3] = imm 11 (story state)");
    }
    {
        // appear conditions: the m501 dual-door pair (flag5 bit10 clear/set)
        std::vector<uint8_t> apClear = hexblk("0105000a000000000000000000000000000000000000000000000000000000");
        std::vector<uint8_t> apSet   = hexblk("0105000a010000000000000000000000000000000000000000000000000000");
        ScriptState st;
        expect(check_event_appear(apClear, st, env) && !check_event_appear(apSet, st, env),
               "appear: flag5[10] clear -> door A only");
        st.setFlag(5, 10, 1);
        expect(!check_event_appear(apClear, st, env) && check_event_appear(apSet, st, env),
               "appear: flag5[10] set -> door B only");
    }
    {
        ScriptState st;                       // save blob round-trip
        st.setFlag(5, 10, 1); st.setVar(2, 7, 12345); st.storyState = 11; st.page = 2;
        st.setVar(1, 3, 777);                 // paged alias into v2
        auto blob = st.serialize();
        ScriptState st2;
        bool ok = st2.deserialize(blob.data(), blob.size());
        expect(ok && st2.getFlag(5, 10) == 1 && st2.getVar(2, 7) == 12345
               && st2.storyState == 11 && st2.page == 2 && st2.getVar(1, 3) == 777,
               "script-state save blob round-trip");
    }
    std::printf("[vmtest] %s (%d failure%s)\n", fails ? "FAILED" : "ALL PASS", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}

// --- NPC auto-wander self-test (--npctest): synthetic map, no bundle needed --
// Verifies the MoveCharaAuto model: a move_type-2 NPC wanders, stays inside
// its event rect, respects collision (wall + player tile), and issues steps at
// the field_constant cadence; a move_type-1 NPC never moves.
static int npcSelfTest() {
    int fails = 0;
    auto expect = [&](bool ok, const char* what) {
        std::printf("[npctest] %-52s %s\n", what, ok ? "PASS" : "FAIL");
        if (!ok) ++fails;
    };
    std::srand(12345);
    FfMap m; m.w = 8; m.h = 8; m.n_layers = 1;
    m.layers.emplace_back(64, (uint16_t)1);
    m.pass.assign(64, 0x0f);
    m.pass[4 * 8 + 4] = 0;                       // wall inside the wander rect
    Event wanderer; wanderer.id = 1; wanderer.x = 2; wanderer.y = 2;
    wanderer.w = 4; wanderer.h = 4;              // rect tiles [2..5]x[2..5]
    wanderer.type = 0; wanderer.boot = 1; wanderer.img = 1;
    wanderer.move_type = 2; wanderer.off_x = 1; wanderer.off_y = 1;   // spawn (3,3)
    wanderer.speed0 = 2; wanderer.freq0 = 2; wanderer.facing0 = 2;    // faces LEFT
    wanderer.scripts.push_back({0x57});
    Event stander = wanderer; stander.id = 2; stander.x = 6; stander.y = 6;
    stander.w = 1; stander.h = 1; stander.off_x = 0; stander.off_y = 0;
    stander.move_type = 1; stander.facing0 = 0;
    m.events.push_back(wanderer);
    m.events.push_back(stander);
    Field f(&m, 32, 0, 0);                       // player parked at (0,0)
    f.enterMap();
    const Actor& a = f.actors()[0];
    const Actor& b = f.actors()[1];
    expect(a.col == 3 && a.row == 3, "FFM6 spawn = rect origin + off_x/off_y");
    expect(a.facing == FACE_LEFT, "FFM6 initial facing applied");
    InputState in{};
    int steps = 0, lc = a.col, lr = a.row;
    bool inRect = true, offWall = true, offPlayer = true, standerStill = true;
    for (int t = 0; t < 6000; ++t) {
        f.update(in);
        if (a.col != lc || a.row != lr) { ++steps; lc = a.col; lr = a.row; }
        if (a.col < 2 || a.col > 5 || a.row < 2 || a.row > 5) inRect = false;
        if (a.col == 4 && a.row == 4) offWall = false;
        if (a.col == 0 && a.row == 0) offPlayer = false;
        if (b.col != 6 || b.row != 6) standerStill = false;
    }
    expect(steps >= 10, "wanderer moved (>= 10 steps in 6000 ticks)");
    expect(steps <= 115, "cadence honours the field_constant wait table");
    expect(inRect, "wanderer stayed inside its event rect");
    expect(offWall, "wanderer never entered the solid tile");
    expect(offPlayer, "wanderer never entered the player tile");
    expect(standerStill, "move_type-1 NPC never moved");
    std::printf("[npctest] %s (%d failure%s)\n", fails ? "FAILED" : "ALL PASS",
                fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}

struct SaveData { bool ok = false; std::string map; int x = 0, y = 0, facing = 0, img = -1;
                  bool hasState = false; std::vector<GameMember> party; std::vector<InvSlot> inv; int gil = 0;
                  std::vector<uint8_t> script;
                  bool dual = false; int partySide = 0; std::array<std::vector<GameMember>, 2> sides; };

static bool writeSave(const std::string& bundle, const std::string& mk, int x, int y, int facing, int img, Host& host) {
    FILE* f = std::fopen((bundle + "/save.dat").c_str(), "wb");
    if (!f) return false;
    std::fwrite("FSAV", 1, 4, f);
    uint8_t ver = 6; std::fwrite(&ver, 1, 1, f);
    uint16_t mlen = (uint16_t)mk.size(); std::fwrite(&mlen, 2, 1, f); std::fwrite(mk.data(), 1, mk.size(), f);
    uint16_t ux = (uint16_t)x, uy = (uint16_t)y; std::fwrite(&ux, 2, 1, f); std::fwrite(&uy, 2, 1, f);
    uint8_t uf = (uint8_t)facing; std::fwrite(&uf, 1, 1, f);
    int32_t im = img; std::fwrite(&im, 4, 1, f);
    // v2: persistent party (current HP/MP), inventory, gil
    const auto& party = host.gameParty();
    uint8_t pc = (uint8_t)party.size(); std::fwrite(&pc, 1, 1, f);
    for (const auto& gm : party) { int32_t v[3] = { gm.charIdx, gm.hp, gm.mp }; std::fwrite(v, 4, 3, f);
                                   int32_t e[6]; for (int k = 0; k < 6; ++k) e[k] = gm.equip[k]; std::fwrite(e, 4, 6, f);
                                   int32_t lv[2] = { gm.level, gm.exp }; std::fwrite(lv, 4, 2, f); }
    const auto& inv = host.inventory();
    uint16_t ic = (uint16_t)inv.size(); std::fwrite(&ic, 2, 1, f);
    for (const auto& sl : inv) { int32_t v[2] = { sl.id, sl.count }; std::fwrite(v, 4, 2, f); }
    int32_t gil = host.gil(); std::fwrite(&gil, 4, 1, f);
    // v5: script flags/vars (event-VM state)
    auto blob = host.scriptState().serialize();
    uint32_t bl = (uint32_t)blob.size(); std::fwrite(&bl, 4, 1, f);
    std::fwrite(blob.data(), 1, blob.size(), f);
    // v6: FFD dual party -- active side + both Light/Dark parties.
    auto wrMember = [&](const GameMember& gm) {
        int32_t v[3] = { gm.charIdx, gm.hp, gm.mp }; std::fwrite(v, 4, 3, f);
        int32_t e[6]; for (int k = 0; k < 6; ++k) e[k] = gm.equip[k]; std::fwrite(e, 4, 6, f);
        int32_t lv[2] = { gm.level, gm.exp }; std::fwrite(lv, 4, 2, f);
    };
    uint8_t side = (uint8_t)host.partySide(); std::fwrite(&side, 1, 1, f);
    const auto& sides = host.parties();
    for (int sIdx = 0; sIdx < 2; ++sIdx) {
        uint8_t sc = (uint8_t)sides[sIdx].size(); std::fwrite(&sc, 1, 1, f);
        for (const auto& gm : sides[sIdx]) wrMember(gm);
    }
    std::fclose(f);
    std::printf("[FFSmith] saved -> %s/save.dat (%s @%d,%d face %d img %d; party %u, inv %u, gil %d)\n",
                bundle.c_str(), mk.c_str(), x, y, facing, img, (unsigned)pc, (unsigned)ic, gil);
    return true;
}

static SaveData readSave(const std::string& bundle) {
    SaveData sd;
    FILE* f = std::fopen((bundle + "/save.dat").c_str(), "rb");
    if (!f) return sd;
    char mag[4];
    if (std::fread(mag, 1, 4, f) != 4 || std::memcmp(mag, "FSAV", 4) != 0) { std::fclose(f); return sd; }
    uint8_t ver = 0; if (std::fread(&ver, 1, 1, f) != 1) { std::fclose(f); return sd; }
    uint16_t mlen = 0; std::fread(&mlen, 2, 1, f);
    sd.map.resize(mlen); if (mlen) std::fread(&sd.map[0], 1, mlen, f);
    uint16_t ux = 0, uy = 0; std::fread(&ux, 2, 1, f); std::fread(&uy, 2, 1, f);
    uint8_t uf = 0; std::fread(&uf, 1, 1, f);
    int32_t im = -1; std::fread(&im, 4, 1, f);
    sd.x = ux; sd.y = uy; sd.facing = uf; sd.img = im; sd.ok = true;
    if (ver >= 2) {
        uint8_t pc = 0; std::fread(&pc, 1, 1, f);
        for (int i = 0; i < pc; ++i) { int32_t v[3] = {0,0,0}; std::fread(v, 4, 3, f); GameMember gm; gm.charIdx = v[0]; gm.hp = v[1]; gm.mp = v[2];
                                       if (ver >= 3) { int32_t e[6] = {0,0,0,0,0,0}; std::fread(e, 4, 6, f); for (int k = 0; k < 6; ++k) gm.equip[k] = e[k]; }
                                       if (ver >= 4) { int32_t lv[2] = {0,0}; std::fread(lv, 4, 2, f); gm.level = lv[0]; gm.exp = lv[1]; }
                                       sd.party.push_back(gm); }
        uint16_t ic = 0; std::fread(&ic, 2, 1, f);
        for (int i = 0; i < ic; ++i) { int32_t v[2] = {0,0}; std::fread(v, 4, 2, f); InvSlot sl; sl.id = v[0]; sl.count = v[1]; sd.inv.push_back(sl); }
        int32_t gil = 0; std::fread(&gil, 4, 1, f); sd.gil = gil; sd.hasState = true;
        if (ver >= 5) {
            uint32_t bl = 0;
            if (std::fread(&bl, 4, 1, f) == 1 && bl > 0 && bl < (1u << 20)) {
                sd.script.resize(bl);
                if (std::fread(sd.script.data(), 1, bl, f) != bl) sd.script.clear();
            }
        }
        if (ver >= 6) {                                   // v6 dual party trailer
            auto rdMember = [&]() { GameMember gm; int32_t v[3] = {0,0,0}; std::fread(v, 4, 3, f);
                gm.charIdx = v[0]; gm.hp = v[1]; gm.mp = v[2];
                int32_t e[6] = {0,0,0,0,0,0}; std::fread(e, 4, 6, f); for (int k = 0; k < 6; ++k) gm.equip[k] = e[k];
                int32_t lv[2] = {0,0}; std::fread(lv, 4, 2, f); gm.level = lv[0]; gm.exp = lv[1]; return gm; };
            uint8_t side = 0; if (std::fread(&side, 1, 1, f) == 1) {
                sd.partySide = side == 1 ? 1 : 0;
                for (int sIdx = 0; sIdx < 2; ++sIdx) { uint8_t sc = 0; std::fread(&sc, 1, 1, f);
                    for (int i = 0; i < sc; ++i) sd.sides[sIdx].push_back(rdMember()); }
                sd.dual = true;
            }
        }
    }
    std::fclose(f);
    return sd;
}

int main(int argc, char** argv) {
    SDL_SetMainReady();
    std::srand((unsigned)std::time(nullptr));
    HostConfig cfg;
    std::string bundle, map, shot, walk, fieldshot;
    bool events_mode = false;
    int playerImg = -1, playerVar = 0, face = -1, startCol = -1, startRow = -1;
    int openMsg = -1, openCnt = 1;
    bool startTitle = false, openMenuFlag = false;
    std::vector<std::string> setFlags;        // --setflag T,I  (debug: set script flag before run)
    std::string startMapKey;                   // New Game opening map (data/start.bin; --start-map overrides)
    bool debugMode = false, dbgNoclip = false, dbgOverlay = false, dbgHud = false;
    int menuPageFlag = 0, battleMon = -1, battleSim = -1;
    bool spellTest = false, saveFlag = false, loadFlag = false, itemTest = false, equipTest = false;
    int animTick = 0; bool dmgTest = false, noOverhead = false, levelTest = false, reviveTest = false, menuTest = false, jobStatTest = false; int introBeat = -1;
    bool encTest = false, encountersOn = false, cutTest = false;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if      (!std::strcmp(a, "--bundle")) bundle = takeStr(argc, argv, i, "");
        else if (!std::strcmp(a, "--map"))    map    = takeStr(argc, argv, i, "");
        else if (!std::strcmp(a, "--shot"))   shot   = takeStr(argc, argv, i, "");
        else if (!std::strcmp(a, "--walk"))   walk   = takeStr(argc, argv, i, "");
        else if (!std::strcmp(a, "--events")) events_mode = true;
        else if (!std::strcmp(a, "--fieldshot")) fieldshot = takeStr(argc, argv, i, "");
        else if (!std::strcmp(a, "--player-img")) playerImg = takeInt(argc, argv, i, playerImg);
        else if (!std::strcmp(a, "--face")) face = takeInt(argc, argv, i, face);
        else if (!std::strcmp(a, "--open-msg")) openMsg = takeInt(argc, argv, i, openMsg);
        else if (!std::strcmp(a, "--open-cnt")) openCnt = takeInt(argc, argv, i, openCnt);
        else if (!std::strcmp(a, "--title")) startTitle = true;
        else if (!std::strcmp(a, "--start-map")) startMapKey = takeStr(argc, argv, i, startMapKey.c_str());
        else if (!std::strcmp(a, "--menu"))  openMenuFlag = true;
        else if (!std::strcmp(a, "--menupage")) menuPageFlag = takeInt(argc, argv, i, 0);
        else if (!std::strcmp(a, "--battle")) battleMon = takeInt(argc, argv, i, 1);
        else if (!std::strcmp(a, "--battlesim")) battleSim = takeInt(argc, argv, i, 1);
        else if (!std::strcmp(a, "--spelltest")) spellTest = true;
        else if (!std::strcmp(a, "--itemtest")) itemTest = true;
        else if (!std::strcmp(a, "--animtick")) animTick = takeInt(argc, argv, i, 0);
        else if (!std::strcmp(a, "--equiptest")) equipTest = true;
        else if (!std::strcmp(a, "--dmgtest")) dmgTest = true;
        else if (!std::strcmp(a, "--leveltest")) levelTest = true;
        else if (!std::strcmp(a, "--jobstattest")) jobStatTest = true;
        else if (!std::strcmp(a, "--revivetest")) reviveTest = true;
        else if (!std::strcmp(a, "--menutest")) menuTest = true;
        else if (!std::strcmp(a, "--enctest")) encTest = true;
        else if (!std::strcmp(a, "--cuttest")) cutTest = true;
        else if (!std::strcmp(a, "--encounters")) encountersOn = true;
        else if (!std::strcmp(a, "--vmtest")) return vmSelfTest();
        else if (!std::strcmp(a, "--npctest")) return npcSelfTest();
        else if (!std::strcmp(a, "--setflag")) setFlags.push_back(takeStr(argc, argv, i, ""));
        else if (!std::strcmp(a, "--intro")) introBeat = takeInt(argc, argv, i, 0);
        else if (!std::strcmp(a, "--no-overhead")) noOverhead = true;
        else if (!std::strcmp(a, "--save")) saveFlag = true;
        else if (!std::strcmp(a, "--load")) loadFlag = true;
        else if (!std::strcmp(a, "--debug")) debugMode = true;
        else if (!std::strcmp(a, "--noclip")) dbgNoclip = true;
        else if (!std::strcmp(a, "--overlay")) dbgOverlay = true;
        else if (!std::strcmp(a, "--hud")) dbgHud = true;
        else if (!std::strcmp(a, "--player")) { const char* v = takeStr(argc, argv, i, ""); std::sscanf(v, "%d,%d", &startCol, &startRow); }
        else if (!std::strcmp(a, "--frames")) cfg.max_frames    = takeInt(argc, argv, i, cfg.max_frames);
        else if (!std::strcmp(a, "--scale"))  cfg.scale         = takeInt(argc, argv, i, cfg.scale);
        else if (!std::strcmp(a, "--aspect")) { const char* v = takeStr(argc, argv, i, ""); int aw=0,ah=0; if (std::sscanf(v, "%d:%d", &aw, &ah)==2 || std::sscanf(v, "%dx%d", &aw, &ah)==2) { cfg.aspectW=aw; cfg.aspectH=ah; } }
        else if (!std::strcmp(a, "--width"))  cfg.logical_width  = takeInt(argc, argv, i, cfg.logical_width);
        else if (!std::strcmp(a, "--height")) cfg.logical_height = takeInt(argc, argv, i, cfg.logical_height);
        else if (!std::strcmp(a, "--hz"))     cfg.tick_hz        = takeInt(argc, argv, i, cfg.tick_hz);
        else if (!std::strcmp(a, "--help"))   { printUsage(argv[0]); return 0; }
        else { std::fprintf(stderr, "[FFSmith] unknown arg: %s\n", a); printUsage(argv[0]); return 2; }
    }
    if (cfg.scale < 1) cfg.scale = 1;
    if (cfg.tick_hz < 1) cfg.tick_hz = 60;

    if (bundle.empty()) { char* base = SDL_GetBasePath(); if (base) { bundle = base; SDL_free(base); } else bundle = "."; }
    std::vector<std::string> mapList = list_maps(bundle);
    std::vector<int> spriteList = list_sprites(bundle);
    if (map.empty()) {
        if (!mapList.empty()) map = mapList.front();             // debug launcher needs an initial map
        else { Host host(cfg); if (!host.init()) return 1; return host.run(); }
    }

    int loadedFacing = -1;
    SaveData bootSave;
    if (loadFlag) {
        bootSave = readSave(bundle);
        if (bootSave.ok) { map = bootSave.map; startCol = bootSave.x; startRow = bootSave.y; if (bootSave.img >= 0) playerImg = bootSave.img; loadedFacing = bootSave.facing;
                     std::printf("[FFSmith] continue: %s @%d,%d\n", map.c_str(), bootSave.x, bootSave.y); }
    }
    FfMap m = load_ffmap(bundle + "/maps/" + map + ".ffmap");
    if (!m.valid()) { std::fprintf(stderr, "[FFSmith] failed to load %s/maps/%s.ffmap\n", bundle.c_str(), map.c_str()); return 1; }
    Texture fb = compose_range(bundle, m, 0, m.overhead_threshold + 1, true);                       // ground = layers [0..threshold]
    Texture overheadFb = compose_range(bundle, m, m.overhead_threshold + 1, m.n_layers, false);  // overhead = layers above threshold
    if (!fb.valid()) { std::fprintf(stderr, "[FFSmith] compose failed for %s\n", map.c_str()); return 1; }
    int tile = (m.w > 0) ? fb.w / m.w : 32;
    std::printf("[FFSmith] %s: %dx%d cells, %d layers (overhead>L%d), %zu events, tile=%d -> %dx%d px\n",
                map.c_str(), m.w, m.h, m.n_layers, m.overhead_threshold, m.events.size(), tile, fb.w, fb.h);
    if (startCol < 0 && m.spawn_x >= 0 && m.spawn_x < m.w) startCol = m.spawn_x;  // map default spawn
    if (startRow < 0 && m.spawn_y >= 0 && m.spawn_y < m.h) startRow = m.spawn_y;
    if (startCol < 0 || startCol >= m.w) startCol = m.w / 2;
    if (startRow < 0 || startRow >= m.h) startRow = m.h / 2;

    if (!shot.empty()) {
        if (!save_tex(shot, fb)) { std::fprintf(stderr, "[FFSmith] failed to write %s\n", shot.c_str()); return 1; }
        std::printf("[FFSmith] wrote framebuffer -> %s\n", shot.c_str());
        return 0;
    }
    if (events_mode) {
        static FfMap evCommon;
        evCommon = load_ffmap(bundle + "/data/common_events.ffmap");
        dump_events(m, tile, evCommon.events.empty() ? nullptr : &evCommon);
        return 0;
    }

    // NPC movement timing tables (baked verbatim from field_constant.dat;
    // decoded defaults compiled in if the bundle predates FFM6).
    Field::setFieldConstant(load_field_constant(bundle + "/data/field_constant.bin"));
    std::unique_ptr<Field> field = std::make_unique<Field>(&m, tile, startCol, startRow);
    if (loadedFacing >= 0) field->setFacing(loadedFacing);
    std::string curMap = map;

    // Reload a map in place (warp). m/fb/field are reassigned; host updated if given.
    auto loadInto = [&](const std::string& key, int sc, int sr, Host* host) -> bool {
        FfMap nm = load_ffmap(bundle + "/maps/" + key + ".ffmap");
        if (!nm.valid()) return false;
        Texture nfb = compose_range(bundle, nm, 0, nm.overhead_threshold + 1, true);
        if (!nfb.valid()) return false;
        m = std::move(nm); fb = std::move(nfb);
        int t = (m.w > 0) ? fb.w / m.w : 32;
        // Map-default spawn (FFM4 header) must be consulted BEFORE the
        // center fallback. The old order applied the center to X first,
        // which put New Game on m0 at (2,1) instead of (1,1) — landing in
        // the WRONG boot-7 intro dispatcher (the dark-world/Nacht chain
        // instead of the light-side prologue). Found by Jack 2026-06-10.
        if (sc < 0 && m.spawn_x >= 0 && m.spawn_x < m.w) sc = m.spawn_x;   // FFM4 map default
        if (sr < 0 && m.spawn_y >= 0 && m.spawn_y < m.h) sr = m.spawn_y;
        if (sc < 0) sc = m.w / 2; else if (sc >= m.w) sc = m.w - 1;
        if (sr < 0) sr = m.h / 2; else if (sr >= m.h) sr = m.h - 1;
        field = std::make_unique<Field>(&m, t, sc, sr);
        if (host) { host->setField(field.get(), fb); host->setOverhead(compose_range(bundle, m, m.overhead_threshold + 1, m.n_layers, false)); host->loadText(bundle, bankOf(key)); }
        if (host) field->enterMap();   // fire on-load autos (cutscenes, parallels)
        curMap = key;
        return true;
    };

    if (!walk.empty()) {
        if (dbgNoclip) field->setNoClip(true);
        // headless trace still needs script state (appear conditions, flags, vars)
        static ScriptState walkState;
        static VMEnv walkEnv; walkEnv.rand = [](int n) { return n > 0 ? (int)(std::rand() % n) : 0; };
        for (const auto& sf : setFlags) {
            int t = 0, ix = 0;
            if (std::sscanf(sf.c_str(), "%d,%d", &t, &ix) == 2) {
                walkState.setFlag(t, ix, 1);
                std::printf("[FFSmith] debug: flag[%d][%d] set\n", t, ix);
            }
        }
        static FfMap walkCommon;
        walkCommon = load_ffmap(bundle + "/data/common_events.ffmap");
        walkEnv.findEvent = [&](int id) -> const Event* {
            for (const auto& e : field->map()->events) if (e.id == id) return &e;
            for (const auto& e : walkCommon.events) if (e.id == id) return &e;
            return nullptr;
        };
        field->setScript(&walkState, &walkEnv);
        field->enterMap();
        static long walkBattleResult = 1;                 // walk traces auto-win battles
        walkEnv.battleRef = [](int type, long) -> long { return type == 3 ? walkBattleResult : 0; };
        auto pumpEnc = [&]() {
            if (field->encounterPending()) {
                VMEncounter enc = field->startEncounter();
                std::printf("  >> SCRIPTED BATTLE formation %d (auto-win, resuming)\n", enc.formation);
                walkBattleResult = 1;
                field->resumeAfterBattle();
            }
        };
        auto wireWalk = [&]() { field->setScript(&walkState, &walkEnv); field->enterMap(); };
        std::printf("[FFSmith] walk trace from (%d,%d) on %dx%d map:\n", startCol, startRow, m.w, m.h);
        for (char ch : walk) {
            if (ch == 'C' || ch == 'c') {              // confirm (advance dialogue / talk)
                InputState cin; cin.pressed = BTN_CONFIRM; field->update(cin);
                std::printf("  C -> dlg=%d msg=%d choice=%d\n",
                            (int)field->inDialogue(), field->dialogueMsg(), (int)field->choiceActive());
                if (!field->inDialogue()) pumpEnc();
                Warp cw = field->consumeWarp();
                if (cw.valid()) {
                    std::string key = find_map_key(bundle, cw.map);
                    if (!key.empty() && loadInto(key, cw.x, cw.y, nullptr)) {
                        wireWalk();
                        std::printf("  >> WARP to %s @(%d,%d)\n", key.c_str(), cw.x, cw.y);
                    } else
                        std::printf("  >> WARP target map %d not found in bundle\n", cw.map);
                }
                continue;
            }
            if (ch == '.') {                            // idle frame (pump autos)
                InputState nin; field->update(nin);
                if (!field->inDialogue()) pumpEnc();
                Warp iw = field->consumeWarp();
                if (iw.valid()) {
                    std::string key = find_map_key(bundle, iw.map);
                    if (!key.empty() && loadInto(key, iw.x, iw.y, nullptr)) {
                        wireWalk();
                        std::printf("  >> WARP to %s @(%d,%d)\n", key.c_str(), iw.x, iw.y);
                    }
                }
                continue;
            }
            int btn = dirBtnFromChar(ch); if (!btn) continue;
            InputState in; in.held = (uint32_t)btn;
            int bc = field->col(), br = field->row();
            field->update(in);
            InputState none; int guard = 0;
            while (field->moving() && guard++ < 100000) field->update(none);
            bool moved = (field->col() != bc || field->row() != br);
            std::printf("  %c -> (%2d,%2d) face=%-5s %s\n", ch, field->col(), field->row(),
                        faceName(field->facing()), moved ? "moved" : "blocked");
            Warp w = field->consumeWarp();
            if (w.valid()) {
                std::string key = find_map_key(bundle, w.map);
                if (!key.empty() && loadInto(key, w.x, w.y, nullptr)) {
                    wireWalk();
                    std::printf("  >> WARP to %s @(%d,%d)\n", key.c_str(), w.x, w.y);
                } else
                    std::printf("  >> WARP target map %d not found in bundle\n", w.map);
            }
        }
        return 0;
    }

    if (playerImg < 0)
        for (const auto& e : m.events) if (e.img > 0) { playerImg = e.img; break; }

    Host host(cfg);
    // Real New Game start point: boot_data scenario record 0 (GameClass::LoadScenarioData).
    int startX = -1, startY = -1;
    {
        auto starts = load_start(bundle + "/data/start.bin");
        if (!starts.empty() && starts[0].valid() && startMapKey.empty()) {
            std::string k = find_map_key(bundle, starts[0].map);
            // x/y stay -1: FieldMapStart passes layer -1, so the map's own
            // default spawn (FFM4 header, FieldClass+0xdc48) decides.
            if (!k.empty()) { startMapKey = k;
                std::printf("[FFSmith] New Game start: scenario 0 -> %s (map default spawn)\n", k.c_str()); }
        }
        if (startMapKey.empty()) startMapKey = "g0_p0_m500";   // fallback (no start.bin)
    }
    if (!host.init()) return 1;
    host.setBundleDir(bundle);
    host.setPlayerSprite(playerImg, playerVar);
    host.loadText(bundle, bankOf(map));
    host.setField(field.get(), fb);
    host.loadCommonEvents(bundle);
    host.setStartMap(startMapKey);
    host.setStartPos(startX, startY);
    for (const auto& sf : setFlags) {
        int t = 0, ix = 0;
        if (std::sscanf(sf.c_str(), "%d,%d", &t, &ix) == 2) {
            host.scriptState().setFlag(t, ix, 1);
            std::printf("[FFSmith] debug: flag[%d][%d] set\n", t, ix);
        }
    }
    if (!noOverhead) host.setOverhead(overheadFb);
    host.setDebugData(mapList, spriteList);
    host.loadMenuData(bundle);
    if (loadFlag && bootSave.hasState) {
        if (bootSave.dual) host.setParties(bootSave.sides, bootSave.partySide); else host.setGameParty(bootSave.party);
        host.setInventory(bootSave.inv); host.setGil(bootSave.gil);
        if (!bootSave.script.empty()) host.scriptState().deserialize(bootSave.script.data(), bootSave.script.size());
        std::printf("[FFSmith] restored party(%zu) inv(%zu) gil=%d script(%zu)\n",
                    bootSave.party.size(), bootSave.inv.size(), bootSave.gil, bootSave.script.size());
    }
    if (itemTest) { host.selfTestItemUse(); return 0; }
    if (equipTest) { host.selfTestEquip(); return 0; }
    if (dmgTest) { host.selfTestDamage(); return 0; }
    if (levelTest) { host.selfTestLevel(); return 0; }
    if (jobStatTest) { host.selfTestJobStat(); return 0; }
    if (reviveTest) { host.selfTestRevive(); return 0; }
    if (menuTest) { host.selfTestMenu(); return 0; }
    if (encTest) { host.setMapKey(map); host.selfTestEncounter(); return 0; }
    if (cutTest) { field->enterMap(); host.selfTestCutscene(); return 0; }
    if (encountersOn) host.setRandomEncounters(true);
    if (saveFlag) { writeSave(bundle, map, startCol, startRow, face < 0 ? 0 : face, playerImg, host); return 0; }
    host.debugSelectMap(map);
    host.setMapKey(map);
    if (openMsg >= 0) field->openMessage(openMsg, openCnt > 0 ? openCnt : 1);
    if (startTitle) { host.loadTitle(bundle); host.setStartMap(startMapKey); host.setStartPos(startX, startY); host.setHasSave(readSave(bundle).ok); host.setMode(Host::Mode::Title); }
    if (openMenuFlag) host.openMenu();
    if (menuPageFlag > 0) host.openMenuPage(menuPageFlag);
    host.setViewFlags(dbgOverlay, dbgHud);
    if (dbgNoclip) field->setNoClip(true);
    if (debugMode) host.setMode(Host::Mode::Debug);
    if (battleMon >= 0) host.startBattle(battleMon);
    if (spellTest) { host.startBattle(1); host.debugOpenMagic(); }
    if (battleSim >= 0) { host.simBattle(battleSim); return 0; }

    if (animTick > 0) host.setAnimTick(animTick);
    if (introBeat >= 0) { host.loadTitle(bundle); host.loadIntro(bundle); host.setIntroState(introBeat); host.setMode(Host::Mode::Intro); }
    if (!fieldshot.empty()) {
        if (face >= 0) { InputState in; in.held = (uint32_t)(face==0?BTN_DOWN:face==1?BTN_UP:face==2?BTN_LEFT:BTN_RIGHT); field->update(in); }
        // --frames N with --fieldshot: build actors + tick the field N times
        // first (lets NPC wander / tile animation advance before the capture).
        if (cfg.max_frames > 0) {
            field->enterMap();
            InputState idle{};
            for (int t = 0; t < cfg.max_frames; ++t) field->update(idle);
        }
        bool ok = host.shotField(fieldshot);
        std::printf("[FFSmith] field shot %s -> %s\n", ok ? "ok" : "FAILED", fieldshot.c_str());
        return ok ? 0 : 1;
    }

    // Interactive default: boot into the main menu (Title). --debug jumps straight to the launcher.
    host.loadTitle(bundle);
    host.loadIntro(bundle);
    host.setStartMap(startMapKey);
    host.setStartPos(startX, startY);
    host.setHasSave(readSave(bundle).ok);          // enable Continue only if a save exists
    host.setMode(debugMode ? Host::Mode::Debug : Host::Mode::Title);

    // Windowed loop: debug-launcher START loads the chosen map; plus cross-map warps.
    while (host.frame()) {
        if (host.consumeSaveRequest()) writeSave(bundle, curMap, field->col(), field->row(), field->facing(), playerImg, host);
        if (host.consumeLoadRequest()) {
            SaveData sd = readSave(bundle);
            if (sd.ok && loadInto(sd.map, sd.x, sd.y, &host)) {
                host.setMapKey(sd.map); curMap = sd.map;
                if (sd.img >= 0) { host.setPlayerSprite(sd.img, 0); playerImg = sd.img; }
                field->setFacing(sd.facing); host.setMode(Host::Mode::Field);
                if (sd.hasState) { if (sd.dual) host.setParties(sd.sides, sd.partySide); else host.setGameParty(sd.party); host.setInventory(sd.inv); host.setGil(sd.gil);
                                   if (!sd.script.empty()) host.scriptState().deserialize(sd.script.data(), sd.script.size()); }
                std::printf("[FFSmith] loaded %s @%d,%d (party %zu, gil %d)\n", sd.map.c_str(), sd.x, sd.y, sd.party.size(), sd.gil);
            }
        }
        Host::DebugStart ds;
        if (host.consumeDebugStart(ds) && !ds.map.empty() && loadInto(ds.map, ds.x, ds.y, &host)) {
            int dimg = ds.img;
            if (dimg < 0) for (const auto& e : m.events) if (e.img > 0) { dimg = e.img; break; }
            host.setPlayerSprite(dimg, 0);
            host.setMapKey(ds.map);
            field->setNoClip(ds.noclip);
            field->setFacing(ds.facing);
            host.setMode(Host::Mode::Field);
            std::printf("[FFSmith] debug start -> %s @(%d,%d) fldchr%d noclip=%d\n",
                        ds.map.c_str(), ds.x, ds.y, ds.img, (int)ds.noclip);
        }
        Warp w = field->consumeWarp();
        if (w.valid()) {
            std::string key = find_map_key(bundle, w.map);
            int fg = host.fadeGen();
            if (!key.empty() && loadInto(key, w.x, w.y, &host)) {
                host.setMapKey(key);
                // The fade-out that preceded this door warp is never followed by the
                // script's fade-in (the warp is handled outside the VM).  If the
                // destination map ran no fade of its own, ramp back in so we don't
                // sit on a black screen until the next debug warp.
                if (host.fadeGen() == fg) host.fadeInAfterWarp();
                std::printf("[FFSmith] warped to %s @(%d,%d) dir %d\n", key.c_str(), w.x, w.y, w.dir);
            } else
                std::fprintf(stderr, "[FFSmith] warp target map %d not found\n", w.map);
        }
    }
    return 0;
}
