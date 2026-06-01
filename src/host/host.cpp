#include "host/host.h"

#include <SDL.h>
#include <cstdio>

namespace ffsmith {

Host::Host(const HostConfig& cfg) : cfg_(cfg) {}

Host::~Host() {
    if (map_tex_)  SDL_DestroyTexture(map_tex_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_)   SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool Host::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "[FFSmith] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // nearest-neighbor only

    const int win_w = cfg_.logical_width  * cfg_.scale;
    const int win_h = cfg_.logical_height * cfg_.scale;
    window_ = SDL_CreateWindow(cfg_.title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               win_w, win_h, SDL_WINDOW_SHOWN);
    if (!window_) {
        std::fprintf(stderr, "[FFSmith] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) {
        std::fprintf(stderr, "[FFSmith] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_RenderSetLogicalSize(renderer_, cfg_.logical_width, cfg_.logical_height);
    std::printf("[FFSmith] init ok: %dx%d x%d @ %d Hz%s\n",
                cfg_.logical_width, cfg_.logical_height, cfg_.scale, cfg_.tick_hz,
                cfg_.max_frames >= 0 ? " (headless)" : "");
    return true;
}

void Host::setMap(const Texture& fb) {
    if (!renderer_ || !fb.valid()) return;
    if (map_tex_) { SDL_DestroyTexture(map_tex_); map_tex_ = nullptr; }
    map_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                 SDL_TEXTUREACCESS_STATIC, fb.w, fb.h);
    if (!map_tex_) {
        std::fprintf(stderr, "[FFSmith] map texture alloc failed: %s\n", SDL_GetError());
        return;
    }
    SDL_UpdateTexture(map_tex_, nullptr, fb.rgba.data(), fb.w * 4);
    has_map_ = true;
    SDL_RenderSetLogicalSize(renderer_, fb.w, fb.h);
    SDL_SetWindowSize(window_, fb.w * cfg_.scale, fb.h * cfg_.scale);
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
            case SDL_QUIT: running_ = false; break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_ESCAPE) { running_ = false; break; }
                if (!ev.key.repeat) raw_held_ |= keyToButton(ev.key.keysym.sym);
                break;
            case SDL_KEYUP:
                raw_held_ &= ~keyToButton(ev.key.keysym.sym);
                break;
            default: break;
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
    ++tick_count_;
    if (cfg_.max_frames >= 0 && static_cast<int>(tick_count_) >= cfg_.max_frames)
        running_ = false;
}

void Host::render() {
    if (has_map_ && map_tex_) {
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_, map_tex_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
        return;
    }
    SDL_SetRenderDrawColor(renderer_, 16, 24, 64, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderPresent(renderer_);
}

int Host::run() {
    running_ = true;
    const double dt = 1.0 / static_cast<double>(cfg_.tick_hz);
    if (cfg_.max_frames >= 0) {
        while (running_) { pumpEvents(); stepInput(); update(dt); render(); }
    } else {
        const uint64_t freq = SDL_GetPerformanceFrequency();
        uint64_t prev = SDL_GetPerformanceCounter();
        double acc = 0.0;
        while (running_) {
            pumpEvents();
            const uint64_t now = SDL_GetPerformanceCounter();
            acc += static_cast<double>(now - prev) / static_cast<double>(freq);
            prev = now;
            if (acc > 0.25) acc = 0.25;
            while (acc >= dt && running_) { stepInput(); update(dt); acc -= dt; }
            render();
        }
    }
    std::printf("[FFSmith] clean exit after %llu ticks\n",
                static_cast<unsigned long long>(tick_count_));
    return 0;
}

}  // namespace ffsmith
