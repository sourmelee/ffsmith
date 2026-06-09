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
        "  --title        boot to the title screen (Start/Z -> field)\n"
        "  --menu         open the field menu (for screenshots)\n"
        "  --debug        boot into the debug launcher (default for windowed)\n"
        "  --noclip/--overlay/--hud   field debug toggles (for screenshots)\n"
        "  --hz N         logic tick rate (default 60)\n"
        "  --help         show this help\n"
        "Controls: arrows/WASD move, Z confirm, Enter/Tab menu, X cancel, Esc quit. Resizable.\n", exe);
}

static void dump_events(const FfMap& m, int tile) {
    std::printf("[FFSmith] map events: %zu\n", m.events.size());
    for (size_t i = 0; i < m.events.size(); ++i) {
        const Event& e = m.events[i];
        std::printf("  ev[%zu] (%2d,%2d) type=%d boot=0x%02x img=%d/%d scripts=%zu\n",
                    i, e.x, e.y, e.type, e.boot, e.img, e.var, e.scripts.size());
        VMOut o = run_event(e);
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

struct SaveData { bool ok = false; std::string map; int x = 0, y = 0, facing = 0, img = -1;
                  bool hasState = false; std::vector<GameMember> party; std::vector<InvSlot> inv; int gil = 0; };

static bool writeSave(const std::string& bundle, const std::string& mk, int x, int y, int facing, int img, const Host& host) {
    FILE* f = std::fopen((bundle + "/save.dat").c_str(), "wb");
    if (!f) return false;
    std::fwrite("FSAV", 1, 4, f);
    uint8_t ver = 4; std::fwrite(&ver, 1, 1, f);
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
    bool debugMode = false, dbgNoclip = false, dbgOverlay = false, dbgHud = false;
    int menuPageFlag = 0, battleMon = -1, battleSim = -1;
    bool spellTest = false, saveFlag = false, loadFlag = false, itemTest = false, equipTest = false;
    int animTick = 0; bool dmgTest = false, noOverhead = false, levelTest = false, reviveTest = false;
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
        else if (!std::strcmp(a, "--revivetest")) reviveTest = true;
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
    if (startCol < 0 || startCol >= m.w) startCol = m.w / 2;
    if (startRow < 0 || startRow >= m.h) startRow = m.h / 2;

    if (!shot.empty()) {
        if (!save_tex(shot, fb)) { std::fprintf(stderr, "[FFSmith] failed to write %s\n", shot.c_str()); return 1; }
        std::printf("[FFSmith] wrote framebuffer -> %s\n", shot.c_str());
        return 0;
    }
    if (events_mode) { dump_events(m, tile); return 0; }

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
        if (sc < 0) sc = m.w / 2; else if (sc >= m.w) sc = m.w - 1;
        if (sr < 0) sr = m.h / 2; else if (sr >= m.h) sr = m.h - 1;
        field = std::make_unique<Field>(&m, t, sc, sr);
        if (host) { host->setField(field.get(), fb); host->setOverhead(compose_range(bundle, m, m.overhead_threshold + 1, m.n_layers, false)); host->loadText(bundle, bankOf(key)); }
        curMap = key;
        return true;
    };

    if (!walk.empty()) {
        if (dbgNoclip) field->setNoClip(true);
        std::printf("[FFSmith] walk trace from (%d,%d) on %dx%d map:\n", startCol, startRow, m.w, m.h);
        for (char ch : walk) {
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
                if (!key.empty() && loadInto(key, w.x, w.y, nullptr))
                    std::printf("  >> WARP to %s @(%d,%d)\n", key.c_str(), w.x, w.y);
                else
                    std::printf("  >> WARP target map %d not found in bundle\n", w.map);
            }
        }
        return 0;
    }

    if (playerImg < 0)
        for (const auto& e : m.events) if (e.img > 0) { playerImg = e.img; break; }

    Host host(cfg);
    if (!host.init()) return 1;
    host.setBundleDir(bundle);
    host.setPlayerSprite(playerImg, playerVar);
    host.loadText(bundle, bankOf(map));
    host.setField(field.get(), fb);
    if (!noOverhead) host.setOverhead(overheadFb);
    host.setDebugData(mapList, spriteList);
    host.loadMenuData(bundle);
    if (loadFlag && bootSave.hasState) {
        host.setGameParty(bootSave.party); host.setInventory(bootSave.inv); host.setGil(bootSave.gil);
        std::printf("[FFSmith] restored party(%zu) inv(%zu) gil=%d\n", bootSave.party.size(), bootSave.inv.size(), bootSave.gil);
    }
    if (itemTest) { host.selfTestItemUse(); return 0; }
    if (equipTest) { host.selfTestEquip(); return 0; }
    if (dmgTest) { host.selfTestDamage(); return 0; }
    if (levelTest) { host.selfTestLevel(); return 0; }
    if (reviveTest) { host.selfTestRevive(); return 0; }
    if (saveFlag) { writeSave(bundle, map, startCol, startRow, face < 0 ? 0 : face, playerImg, host); return 0; }
    host.debugSelectMap(map);
    host.setMapKey(map);
    if (openMsg >= 0) field->openMessage(openMsg, openCnt > 0 ? openCnt : 1);
    if (startTitle) { host.loadTitle(bundle); host.setMode(Host::Mode::Title); }
    if (openMenuFlag) host.openMenu();
    if (menuPageFlag > 0) host.openMenuPage(menuPageFlag);
    host.setViewFlags(dbgOverlay, dbgHud);
    if (dbgNoclip) field->setNoClip(true);
    if (debugMode) host.setMode(Host::Mode::Debug);
    if (battleMon >= 0) host.startBattle(battleMon);
    if (spellTest) { host.startBattle(1); host.debugOpenMagic(); }
    if (battleSim >= 0) { host.simBattle(battleSim); return 0; }

    if (animTick > 0) host.setAnimTick(animTick);
    if (!fieldshot.empty()) {
        if (face >= 0) { InputState in; in.held = (uint32_t)(face==0?BTN_DOWN:face==1?BTN_UP:face==2?BTN_LEFT:BTN_RIGHT); field->update(in); }
        bool ok = host.shotField(fieldshot);
        std::printf("[FFSmith] field shot %s -> %s\n", ok ? "ok" : "FAILED", fieldshot.c_str());
        return ok ? 0 : 1;
    }

    // Interactive default: boot into the debug launcher (unless --title was given).
    if (!startTitle) host.setMode(Host::Mode::Debug);

    // Windowed loop: debug-launcher START loads the chosen map; plus cross-map warps.
    while (host.frame()) {
        if (host.consumeSaveRequest()) writeSave(bundle, curMap, field->col(), field->row(), field->facing(), playerImg, host);
        if (host.consumeLoadRequest()) {
            SaveData sd = readSave(bundle);
            if (sd.ok && loadInto(sd.map, sd.x, sd.y, &host)) {
                host.setMapKey(sd.map); curMap = sd.map;
                if (sd.img >= 0) { host.setPlayerSprite(sd.img, 0); playerImg = sd.img; }
                field->setFacing(sd.facing); host.setMode(Host::Mode::Field);
                if (sd.hasState) { host.setGameParty(sd.party); host.setInventory(sd.inv); host.setGil(sd.gil); }
                std::printf("[FFSmith] loaded %s @%d,%d (party %zu, gil %d)\n", sd.map.c_str(), sd.x, sd.y, sd.party.size(), sd.gil);
            }
        }
        Host::DebugStart ds;
        if (host.consumeDebugStart(ds) && !ds.map.empty() && loadInto(ds.map, ds.x, ds.y, &host)) {
            host.setPlayerSprite(ds.img, 0);
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
            if (!key.empty() && loadInto(key, w.x, w.y, &host)) {
                host.setMapKey(key);
                std::printf("[FFSmith] warped to %s @(%d,%d)\n", key.c_str(), w.x, w.y);
            } else
                std::fprintf(stderr, "[FFSmith] warp target map %d not found\n", w.map);
        }
    }
    return 0;
}
