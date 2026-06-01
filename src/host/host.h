#pragma once
#include <cstdint>
#include "host/input.h"

// Forward declarations (in C++ a struct tag is usable as a type name),
// so this header does not need to pull in <SDL.h>.
struct SDL_Window;
struct SDL_Renderer;

namespace ffsmith {

struct HostConfig {
    int logical_width  = 240;  // native playfield width  (TODO: confirm FFD/FFL exact)
    int logical_height = 160;  // native playfield height (TODO: confirm)
    int scale          = 3;    // integer window upscale (pixel-art: nearest only)
    int tick_hz        = 60;   // fixed logic rate (TODO: confirm original tick rate)
    int max_frames     = -1;   // -1 = run until quit; >=0 = headless, exit after N ticks
    const char* title  = "FFSmith";
};

// Owns the window, renderer, input, and the main loop. This is the modern stand-in
// for the original's platform glue (JNI MainActivity_render / _touch). Gameplay
// subsystems (field, battle, menu) will be ticked from update()/render() later.
class Host {
public:
    explicit Host(const HostConfig& cfg);
    ~Host();
    Host(const Host&)            = delete;
    Host& operator=(const Host&) = delete;

    bool init();   // create window + renderer; false on failure
    int  run();    // main loop; returns a process exit code

    const InputState& input() const { return input_; }
    SDL_Renderer* renderer() const { return renderer_; }

private:
    void pumpEvents();      // SDL events -> raw_held_ / quit
    void stepInput();       // compute held/pressed/released edges for one tick
    void update(double dt); // fixed-step logic (M0: tick counter only)
    void render();          // draw one frame (M0: clear)

    HostConfig    cfg_;
    SDL_Window*   window_     = nullptr;
    SDL_Renderer* renderer_   = nullptr;
    InputState    input_;
    uint32_t      raw_held_   = 0;
    uint32_t      held_prev_  = 0;
    bool          running_    = false;
    uint64_t      tick_count_ = 0;
};

} // namespace ffsmith
