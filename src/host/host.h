#pragma once
#include <cstdint>
#include "host/input.h"
#include "data/bundle.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace ffsmith {

struct HostConfig {
    int logical_width  = 240;
    int logical_height = 160;
    int scale          = 3;
    int tick_hz        = 60;
    int max_frames     = -1;   // -1 = run until quit; >=0 = headless, exit after N ticks
    const char* title  = "FFSmith";
};

class Host {
public:
    explicit Host(const HostConfig& cfg);
    ~Host();
    Host(const Host&)            = delete;
    Host& operator=(const Host&) = delete;

    bool init();
    int  run();
    void setMap(const Texture& fb);   // M1: display a pre-composed map

    const InputState& input() const { return input_; }
    SDL_Renderer* renderer() const { return renderer_; }

private:
    void pumpEvents();
    void stepInput();
    void update(double dt);
    void render();

    HostConfig    cfg_;
    SDL_Window*   window_     = nullptr;
    SDL_Renderer* renderer_   = nullptr;
    SDL_Texture*  map_tex_    = nullptr;
    bool          has_map_    = false;
    InputState    input_;
    uint32_t      raw_held_   = 0;
    uint32_t      held_prev_  = 0;
    bool          running_    = false;
    uint64_t      tick_count_ = 0;
};

}  // namespace ffsmith
