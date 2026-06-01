#include "host/host.h"

#include <SDL.h>
#include <cstdio>

namespace ffsmith {

Host::Host(const HostConfig& cfg) : cfg_(cfg) {}

Host::~Host() {
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_)   SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool Host::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "[FFSmith] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    // Pixel-art rule: integer nearest-neighbor scaling ONLY. Never linear.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    const int win_w = cfg_.logical_width  * cfg_.scale;
    const int win_h = cfg_.logical_height * cfg_.scale;

    window_ = SDL_CreateWindow(cfg_.title,
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               win_w, win_h, SDL_WINDOW_SHOWN);
    if (!window_) {
        std::fprintf(stderr, "[FFSmith] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1,
                                   SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        // Headless / no-GPU fallback (e.g. SDL dummy video driver in CI).
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer_) {
        std::fprintf(stderr, "[FFSmith] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    // Draw in native logical resolution; SDL integer-scales to the window.
    SDL_RenderSetLogicalSize(renderer_, cfg_.logical_width, cfg_.logical_height);

    std::printf("[FFSmith] init ok: %dx%d x%d @ %d Hz%s\n",
                cfg_.logical_width, cfg_.logical_height, cfg_.scale, cfg_.tick_hz,
                cfg_.max_frames >= 0 ? " (headless)" : "");
    return true;
}

static uint32_t keyToButton(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP:    case SDLK_w: return BTN_UP;
        case SDLK_DOWN:  case SDLK_s: return BTN_DOWN;
        case SDLK_LEFT:  case SDLK_a: return BTN_LEFT;
        case SDLK_RIGHT: case SDLK_d: return BTN_RIGHT;
        case SDLK_z: case SDLK_SPACE: return BTN_CONFIRM;
        case SDLK_x: case SDLK_BACKSPACE: return BTN_CANCEL;
        case SDLK_RETURN:             return BTN_MENU;
        case SDLK_q:                  return BTN_L;
        case SDLK_e:                  return BTN_R;
        default:                      return BTN_NONE;
    }
}

void Host::pumpEvents() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                running_ = false;
                break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_ESCAPE) { running_ = false; break; }
                if (!ev.key.repeat) raw_held_ |= keyToButton(ev.key.keysym.sym);
                break;
            case SDL_KEYUP:
                raw_held_ &= ~keyToButton(ev.key.keysym.sym);
                break;
            default:
                break;
        }
    }
}

void Host::stepInput() {
    input_.held     = raw_held_;
    input_.pressed  = raw_held_ & ~held_prev_;
    input_.released = held_prev_ & ~raw_held_;
    held_prev_      = raw_held_;
}

void Host::update(double /*dt*/) {
    // M0: no gameplay yet — just advance the logic tick counter.
    ++tick_count_;
    if (cfg_.max_frames >= 0 && static_cast<int>(tick_count_) >= cfg_.max_frames) {
        running_ = false;
    }
}

void Host::render() {
    // M0: clear to FF-menu navy. Real drawing (static map) arrives at M1.
    SDL_SetRenderDrawColor(renderer_, 16, 24, 64, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderPresent(renderer_);
}

int Host::run() {
    running_ = true;
    const double dt = 1.0 / static_cast<double>(cfg_.tick_hz);

    if (cfg_.max_frames >= 0) {
        // Headless / deterministic: step as fast as possible, no real-time wait.
        while (running_) {
            pumpEvents();
            stepInput();
            update(dt);
            render();
        }
    } else {
        // Real-time fixed-timestep loop.
        const uint64_t freq = SDL_GetPerformanceFrequency();
        uint64_t prev = SDL_GetPerformanceCounter();
        double acc = 0.0;
        while (running_) {
            pumpEvents();
            const uint64_t now = SDL_GetPerformanceCounter();
            acc += static_cast<double>(now - prev) / static_cast<double>(freq);
            prev = now;
            if (acc > 0.25) acc = 0.25; // clamp: avoid spiral of death
            while (acc >= dt && running_) {
                stepInput();
                update(dt);
                acc -= dt;
            }
            render();
        }
    }

    std::printf("[FFSmith] clean exit after %llu ticks\n",
                static_cast<unsigned long long>(tick_count_));
    return 0;
}

} // namespace ffsmith
