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
#include <string>

using namespace ffsmith;

static int takeInt(int argc, char** argv, int& i, int fb) { if (i+1<argc) return std::atoi(argv[++i]); return fb; }
static const char* takeStr(int argc, char** argv, int& i, const char* fb) { if (i+1<argc) return argv[++i]; return fb; }
static int dirBtnFromChar(char ch) {
    switch (ch) { case 'U': case 'u': return BTN_UP; case 'D': case 'd': return BTN_DOWN;
                  case 'L': case 'l': return BTN_LEFT; case 'R': case 'r': return BTN_RIGHT; default: return 0; }
}
static const char* faceName(int f) { static const char* n[4]={"down","up","left","right"}; return (f>=0&&f<4)?n[f]:"?"; }

static void printUsage(const char* exe) {
    std::printf(
        "FFSmith - clean-room engine for Final Fantasy Dimensions / Legends\n"
        "Usage: %s [options]\n"
        "  --map KEY      map to load, e.g. g0_p0_m501 (bundle defaults to exe folder)\n"
        "  --bundle DIR   asset bundle dir (has maps/ and tex/); default: exe folder\n"
        "  --player C,R   start tile (default: map centre)\n"
        "  --shot PATH    compose map, write .tex, exit (headless)\n"
        "  --walk SCRIPT  headless: feed U/D/L/R steps, print player path, exit\n"
        "  --events       headless: list events + dry-run their scripts + a talk sim, exit\n"
        "  --player-img N fldchr id for the player sprite (default: first NPC sprite)\n"
        "  --fieldshot P  render one field frame to a .tex and exit (headless)\n"
        "  --frames N     run N ticks then exit\n"
        "  --scale N      zoom factor (default 3); resize the window to see more map\n"
        "  --hz N         logic tick rate (default 60)\n"
        "  --help         show this help\n"
        "Controls: arrows/WASD move, Z/Enter talk/confirm, Esc quit. Window is resizable.\n", exe);
}

static void dump_events(const FfMap& m, int tile) {
    std::printf("[FFSmith] %s events: %d\n", "map", (int)m.events.size());
    for (size_t i = 0; i < m.events.size(); ++i) {
        const Event& e = m.events[i];
        std::printf("  ev[%zu] (%2d,%2d) type=%d boot=0x%02x img=%d/%d scripts=%zu\n",
                    i, e.x, e.y, e.type, e.boot, e.img, e.var, e.scripts.size());
        VMOut o = run_event(e);
        for (const auto& l : o.log) std::printf("       . %s\n", l.c_str());
        if (!o.messages.empty()) {
            std::printf("       => messages:");
            for (int id : o.messages) std::printf(" %d", id);
            std::printf("\n");
        }
    }
    // Talk simulation against the first visible NPC with scripts.
    const Event* npc = nullptr;
    for (const auto& e : m.events) if (e.img > 0 && !e.scripts.empty()) { npc = &e; break; }
    if (npc) {
        struct A { int dx, dy; uint32_t btn; } adj[4] =
            {{0,1,BTN_UP},{0,-1,BTN_DOWN},{1,0,BTN_LEFT},{-1,0,BTN_RIGHT}};
        for (auto& a : adj) {
            int sx = npc->x + a.dx, sy = npc->y + a.dy;
            if (sx < 0 || sy < 0 || sx >= m.w || sy >= m.h) continue;
            Field f(&m, tile, sx, sy);
            if (f.isSolid(sx, sy)) continue;          // must stand on open ground
            InputState face; face.held = a.btn; f.update(face);     // turn toward NPC (blocked)
            InputState conf; conf.pressed = BTN_CONFIRM; f.update(conf);  // talk
            std::printf("[sim] stand (%d,%d) face %s -> NPC(%d,%d): inDialogue=%d msg=%d\n",
                        sx, sy, faceName(f.facing()), npc->x, npc->y, f.inDialogue(), f.dialogueMsg());
            break;
        }
    }
}

int main(int argc, char** argv) {
    SDL_SetMainReady();
    HostConfig cfg;
    std::string bundle, map, shot, walk, fieldshot;
    bool events_mode = false;
    int playerImg = -1, playerVar = 0, face = -1;
    int startCol = -1, startRow = -1;
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

    if (map.empty()) { Host host(cfg); if (!host.init()) return 1; return host.run(); }

    if (bundle.empty()) { char* base = SDL_GetBasePath(); if (base) { bundle = base; SDL_free(base); } else bundle = "."; }

    const std::string ffmap_path = bundle + "/maps/" + map + ".ffmap";
    FfMap m = load_ffmap(ffmap_path);
    if (!m.valid()) { std::fprintf(stderr, "[FFSmith] failed to load %s\n", ffmap_path.c_str()); return 1; }
    Texture fb = compose_map(bundle, m);
    if (!fb.valid()) { std::fprintf(stderr, "[FFSmith] compose failed for %s\n", map.c_str()); return 1; }
    const int tile = (m.w > 0) ? fb.w / m.w : 32;
    std::printf("[FFSmith] %s: %dx%d cells, %d layers, %zu events, tile=%d -> %dx%d px\n",
                map.c_str(), m.w, m.h, m.n_layers, m.events.size(), tile, fb.w, fb.h);

    if (startCol < 0 || startCol >= m.w) startCol = m.w / 2;
    if (startRow < 0 || startRow >= m.h) startRow = m.h / 2;

    if (!shot.empty()) {
        if (!save_tex(shot, fb)) { std::fprintf(stderr, "[FFSmith] failed to write %s\n", shot.c_str()); return 1; }
        std::printf("[FFSmith] wrote framebuffer -> %s\n", shot.c_str());
        return 0;
    }
    if (events_mode) { dump_events(m, tile); return 0; }

    Field field(&m, tile, startCol, startRow);

    if (!walk.empty()) {
        std::printf("[FFSmith] walk trace from (%d,%d) on %dx%d map:\n", startCol, startRow, m.w, m.h);
        for (char ch : walk) {
            int btn = dirBtnFromChar(ch); if (!btn) continue;
            InputState in; in.held = (uint32_t)btn;
            int bc = field.col(), br = field.row();
            field.update(in);
            InputState none; int guard = 0;
            while (field.moving() && guard++ < 100000) field.update(none);
            bool moved = (field.col() != bc || field.row() != br);
            std::printf("  %c -> (%2d,%2d) face=%-5s %s\n", ch, field.col(), field.row(),
                        faceName(field.facing()), moved ? "moved" : "blocked");
        }
        return 0;
    }

    if (playerImg < 0)
        for (const auto& e : m.events) if (e.img > 0) { playerImg = e.img; break; }

    Host host(cfg);
    if (!host.init()) return 1;
    host.setBundleDir(bundle);
    host.setPlayerSprite(playerImg, playerVar);
    host.setField(&field, fb);
    if (face >= 0) { InputState in; in.held = (uint32_t)(face==0?BTN_DOWN:face==1?BTN_UP:face==2?BTN_LEFT:BTN_RIGHT); field.update(in); }
    if (!fieldshot.empty()) {
        bool ok = host.shotField(fieldshot);
        std::printf("[FFSmith] field shot %s -> %s\n", ok ? "ok" : "FAILED", fieldshot.c_str());
        return ok ? 0 : 1;
    }
    return host.run();
}
