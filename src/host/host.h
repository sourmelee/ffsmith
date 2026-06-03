#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include "host/input.h"
#include "data/bundle.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace ffsmith {

class Field;

struct HostConfig {
    int logical_width  = 256;
    int logical_height = 176;
    int scale          = 3;
    int tick_hz        = 60;
    int max_frames     = -1;
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
    bool frame();   // one loop iteration; false when quit (lets caller swap maps on warp)
    void setMap(const Texture& fb);
    void setField(Field* f, const Texture& mapImg);
    void setBundleDir(const std::string& d) { bundleDir_ = d; }
    void setPlayerSprite(int img, int var) { playerImg_ = img; playerVar_ = var; }
    bool shotField(const std::string& path);   // render one field frame, read pixels, save .tex

    const InputState& input() const { return input_; }
    SDL_Renderer* renderer() const { return renderer_; }

private:
    void pumpEvents();
    void stepInput();
    void update(double dt);
    void render();
    bool ensureMapTexture(const Texture& img);
    SDL_Texture* spriteTex(int img, int var, int& w, int& h);  // cached fldchr sheet
    bool drawSprite(int img, int var, int facing, int animCol, int lx, int ly, int tile);

    HostConfig    cfg_;
    SDL_Window*   window_     = nullptr;
    SDL_Renderer* renderer_   = nullptr;
    SDL_Texture*  map_tex_    = nullptr;
    bool          has_map_    = false;
    Field*        field_      = nullptr;
    int           mapW_ = 0, mapH_ = 0;
    std::string   bundleDir_;
    int           playerImg_ = -1, playerVar_ = 0;
    std::unordered_map<int, SDL_Texture*> sprites_;   // key = img*100+var
    InputState    input_;
    uint32_t      raw_held_   = 0;
    uint32_t      held_prev_  = 0;
    bool          running_    = false;
    uint64_t      tick_count_ = 0;
    bool          started_ = false;
    uint64_t      prevCtr_ = 0;
    double        acc_ = 0.0;
};

}  // namespace ffsmith
