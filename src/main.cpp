#include "host/host.h"
#include "data/bundle.h"
#include "render/compositor.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace ffsmith;

static int takeInt(int argc, char** argv, int& i, int fallback) {
    if (i + 1 < argc) return std::atoi(argv[++i]);
    return fallback;
}
static const char* takeStr(int argc, char** argv, int& i, const char* fallback) {
    if (i + 1 < argc) return argv[++i];
    return fallback;
}

static void printUsage(const char* exe) {
    std::printf(
        "FFSmith - clean-room engine for Final Fantasy Dimensions / Legends\n"
        "Usage: %s [options]\n"
        "  --bundle DIR   asset bundle baked by the toolkit (--bake-ffsmith)\n"
        "  --map KEY      map id to load, e.g. g0_p0_m501\n"
        "  --shot PATH    compose the map, write it as a .tex, and exit (headless)\n"
        "  --frames N     run N logic ticks then exit (headless/CI)\n"
        "  --scale N      integer window upscale (default 3)\n"
        "  --hz N         logic tick rate (default 60)\n"
        "  --help         show this help\n", exe);
}

int main(int argc, char** argv) {
    HostConfig cfg;
    std::string bundle, map, shot;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if      (std::strcmp(a, "--bundle") == 0) bundle = takeStr(argc, argv, i, "");
        else if (std::strcmp(a, "--map")    == 0) map    = takeStr(argc, argv, i, "");
        else if (std::strcmp(a, "--shot")   == 0) shot   = takeStr(argc, argv, i, "");
        else if (std::strcmp(a, "--frames") == 0) cfg.max_frames    = takeInt(argc, argv, i, cfg.max_frames);
        else if (std::strcmp(a, "--scale")  == 0) cfg.scale         = takeInt(argc, argv, i, cfg.scale);
        else if (std::strcmp(a, "--width")  == 0) cfg.logical_width  = takeInt(argc, argv, i, cfg.logical_width);
        else if (std::strcmp(a, "--height") == 0) cfg.logical_height = takeInt(argc, argv, i, cfg.logical_height);
        else if (std::strcmp(a, "--hz")     == 0) cfg.tick_hz        = takeInt(argc, argv, i, cfg.tick_hz);
        else if (std::strcmp(a, "--help")   == 0) { printUsage(argv[0]); return 0; }
        else { std::fprintf(stderr, "[FFSmith] unknown arg: %s\n", a); printUsage(argv[0]); return 2; }
    }
    if (cfg.scale   < 1) cfg.scale   = 1;
    if (cfg.tick_hz < 1) cfg.tick_hz = 60;

    Texture fb;
    if (!bundle.empty() && !map.empty()) {
        const std::string ffmap_path = bundle + "/maps/" + map + ".ffmap";
        FfMap m = load_ffmap(ffmap_path);
        if (!m.valid()) {
            std::fprintf(stderr, "[FFSmith] failed to load %s\n", ffmap_path.c_str());
            return 1;
        }
        fb = compose_map(bundle, m);
        if (!fb.valid()) {
            std::fprintf(stderr, "[FFSmith] compose failed for %s\n", map.c_str());
            return 1;
        }
        std::printf("[FFSmith] %s: %dx%d cells, %d layers, slots [%d/%d][%d/%d] -> %dx%d px\n",
                    map.c_str(), m.w, m.h, m.n_layers,
                    m.mc_slot0, m.var_slot0, m.mc_slot1, m.var_slot1, fb.w, fb.h);
        if (!shot.empty()) {
            if (!save_tex(shot, fb)) {
                std::fprintf(stderr, "[FFSmith] failed to write %s\n", shot.c_str());
                return 1;
            }
            std::printf("[FFSmith] wrote framebuffer -> %s\n", shot.c_str());
            return 0;
        }
    } else if (!bundle.empty() || !map.empty() || !shot.empty()) {
        std::fprintf(stderr, "[FFSmith] --bundle and --map must be given together\n");
        return 2;
    }

    Host host(cfg);
    if (!host.init()) return 1;
    if (fb.valid()) host.setMap(fb);
    return host.run();
}
