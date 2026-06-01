#include "host/host.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace ffsmith;

static int takeInt(int argc, char** argv, int& i, int fallback) {
    if (i + 1 < argc) return std::atoi(argv[++i]);
    return fallback;
}

static void printUsage(const char* exe) {
    std::printf(
        "FFSmith - clean-room engine for Final Fantasy Dimensions / Legends\n"
        "Usage: %s [options]\n"
        "  --frames N   run N logic ticks then exit (headless/CI; default: run until quit)\n"
        "  --scale N    integer window upscale (default 3)\n"
        "  --width N    logical width  (default 240)\n"
        "  --height N   logical height (default 160)\n"
        "  --hz N       logic tick rate (default 60)\n"
        "  --help       show this help\n"
        "Controls: arrows/WASD = move, Z/Space = confirm, X = cancel, Enter = menu, Esc = quit\n",
        exe);
}

int main(int argc, char** argv) {
    HostConfig cfg;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if      (std::strcmp(a, "--frames") == 0) cfg.max_frames    = takeInt(argc, argv, i, cfg.max_frames);
        else if (std::strcmp(a, "--scale")  == 0) cfg.scale         = takeInt(argc, argv, i, cfg.scale);
        else if (std::strcmp(a, "--width")  == 0) cfg.logical_width  = takeInt(argc, argv, i, cfg.logical_width);
        else if (std::strcmp(a, "--height") == 0) cfg.logical_height = takeInt(argc, argv, i, cfg.logical_height);
        else if (std::strcmp(a, "--hz")     == 0) cfg.tick_hz        = takeInt(argc, argv, i, cfg.tick_hz);
        else if (std::strcmp(a, "--help")   == 0) { printUsage(argv[0]); return 0; }
        else {
            std::fprintf(stderr, "[FFSmith] unknown arg: %s\n", a);
            printUsage(argv[0]);
            return 2;
        }
    }
    if (cfg.scale   < 1) cfg.scale   = 1;
    if (cfg.tick_hz < 1) cfg.tick_hz = 60;

    Host host(cfg);
    if (!host.init()) return 1;
    return host.run();
}
