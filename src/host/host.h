#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
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

    enum class Mode { Debug, Title, Field };
    struct DebugStart { std::string map; int img = -1, x = 0, y = 0, facing = 0; bool noclip = false; };

    bool init();
    int  run();
    bool frame();   // one loop iteration; false when quit (lets caller swap maps on warp)
    void setMap(const Texture& fb);
    void setField(Field* f, const Texture& mapImg);
    void setBundleDir(const std::string& d) { bundleDir_ = d; }
    void setPlayerSprite(int img, int var) { playerImg_ = img; playerVar_ = var; }
    void setMode(Mode m) { mode_ = m; }
    void openMenu() { menuOpen_ = true; menuCursor_ = 0; menuPage_ = 0; }
    bool loadMenuData(const std::string& bundleDir);   // data/items.bin + chars.bin
    void openMenuPage(int pg) { menuOpen_ = true; menuPage_ = pg; pageCursor_ = 0; pageScroll_ = 0; pageChar_ = 0; }
    bool loadTitle(const std::string& bundleDir);   // ui/title.tex
    void setDebugData(std::vector<std::string> maps, std::vector<int> sprites);
    void debugSelectMap(const std::string& key);
    void setMapKey(const std::string& key) { mapKey_ = key; }   // for the HUD
    void setViewFlags(bool overlay, bool hud) { overlayOn_ = overlay; hudOn_ = hud; }
    bool consumeDebugStart(DebugStart& out);
    bool shotField(const std::string& path);   // render one field frame, read pixels, save .tex
    bool loadText(const std::string& bundleDir, int bank);   // load text/msg{bank}.bin + font

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
    void updateMenu(const InputState& in);
    void renderTitle();
    void renderMenu(int vw, int vh);
    void renderItemPage(int px, int py, int pw, int ph);
    void renderCharPage(int px, int py, int pw, int ph, bool status);
    void updateDebug(const InputState& in);
    void renderDebug();
    void drawText(int x, int y, const std::string& s, int maxChars, uint8_t r, uint8_t g, uint8_t b);

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
    SDL_Texture*  fontTex_ = nullptr;
    int           fcw_ = 0, fch_ = 0, fcols_ = 0, ffirst_ = 32;
    std::unordered_map<int, std::string> messages_;
    Mode          mode_ = Mode::Field;
    bool          menuOpen_ = false;
    int           menuCursor_ = 0;
    int           blink_ = 0;
    SDL_Texture*  titleTex_ = nullptr;
    int           titleW_ = 0, titleH_ = 0;
    std::vector<std::string> dbgMaps_;
    std::vector<int> dbgSprites_;
    int  dbgRow_ = 0, dbgMapIdx_ = 0, dbgSprIdx_ = 0;
    int  dbgX_ = 0, dbgY_ = 0, dbgFacing_ = 0;
    bool dbgNoclip_ = false, dbgOverlay_ = false, dbgHud_ = true, dbgStart_ = false;
    bool overlayOn_ = false, hudOn_ = false;
    std::string mapKey_;
    std::unordered_map<int, Item> items_;
    std::vector<int> itemIds_;
    std::vector<CharRec> chars_;
    int menuPage_ = 0, pageCursor_ = 0, pageScroll_ = 0, pageChar_ = 0;
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
