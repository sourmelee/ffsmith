#include "host/host.h"
#include <cctype>

#include <SDL.h>
#include <cstdio>
#include <string>
#include <algorithm>
#include <cstdlib>
#include "field/field.h"
#include "field/event_vm.h"

namespace ffsmith {

namespace { const char* kMenuItems[] = {"Item", "Magic", "Equip", "Job", "Status", "Formation", "Config", "Save", "Quit"}; const int kMenuN = 9; }
// Display aspect presets (name, w, h).  0,0 = free-form resizable window.  Includes
// vertical ratios for a future portrait/Android layout.
namespace { struct AspectPreset { const char* name; int w, h; };
  const AspectPreset kAspects[] = {
    {"Free-form", 0, 0}, {"4:3", 4, 3}, {"5:4", 5, 4}, {"16:9", 16, 9}, {"16:10", 16, 10},
    {"1:1", 1, 1}, {"9:16 (vert)", 9, 16}, {"10:16 (vert)", 10, 16} };
  const int kAspectN = (int)(sizeof(kAspects) / sizeof(kAspects[0])); }
// Window colour presets: 3-stop vertical gradient (top / mid / bottom RGB).
// Index 0 = the faithful FFD blue (GameClass::DrawWindow); the rest are tints.
namespace { struct WinColor { const char* name; int tr,tg,tb, mr,mg,mb, br,bg,bb; };
  const WinColor kWinColors[] = {
    {"Blue",    63,69,134, 42,43,102, 75,78,122},
    {"Teal",    36,96,110, 22,60,74,  46,104,110},
    {"Purple",  84,58,128, 54,36,92,  96,72,134},
    {"Crimson", 128,46,58, 90,30,40,  134,64,72},
    {"Forest",  44,96,60,  28,64,42,  56,104,70},
    {"Slate",   60,66,80,  40,44,56,  72,78,92} };
  const int kWinColorN = (int)(sizeof(kWinColors)/sizeof(kWinColors[0])); }

Host::Host(const HostConfig& cfg) : cfg_(cfg) { dbgScale_ = cfg_.scale; }

Host::~Host() {
    for (auto& kv : sprites_) if (kv.second) SDL_DestroyTexture(kv.second);
    if (fontTex_)  SDL_DestroyTexture(fontTex_);
    if (titleTex_) SDL_DestroyTexture(titleTex_);
    if (btlbgTex_) SDL_DestroyTexture(btlbgTex_);
    if (map_tex_)  SDL_DestroyTexture(map_tex_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_)   SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool Host::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "[FFSmith] SDL_Init failed: %s\n", SDL_GetError()); return false;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    const int win_w = cfg_.logical_width * cfg_.scale;
    const int win_h = cfg_.logical_height * cfg_.scale;
    window_ = SDL_CreateWindow(cfg_.title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               win_w, win_h, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window_) { std::fprintf(stderr, "[FFSmith] SDL_CreateWindow failed: %s\n", SDL_GetError()); return false; }
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) { std::fprintf(stderr, "[FFSmith] SDL_CreateRenderer failed: %s\n", SDL_GetError()); return false; }
    std::printf("[FFSmith] init ok: window %dx%d, zoom x%d @ %d Hz%s\n",
                win_w, win_h, cfg_.scale, cfg_.tick_hz, cfg_.max_frames >= 0 ? " (headless)" : "");
    if (cfg_.aspectW > 0 && cfg_.aspectH > 0) setAspect(cfg_.aspectW, cfg_.aspectH);  // --aspect at startup
    return true;
}

bool Host::ensureMapTexture(const Texture& img) {
    if (!renderer_ || !img.valid()) return false;
    if (map_tex_) { SDL_DestroyTexture(map_tex_); map_tex_ = nullptr; }
    map_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
    if (!map_tex_) { std::fprintf(stderr, "[FFSmith] map texture alloc failed: %s\n", SDL_GetError()); return false; }
    SDL_UpdateTexture(map_tex_, nullptr, img.rgba.data(), img.w * 4);
    mapW_ = img.w; mapH_ = img.h;
    return true;
}

void Host::setMap(const Texture& fb) {
    if (!ensureMapTexture(fb)) return;
    has_map_ = true;
    SDL_RenderSetLogicalSize(renderer_, fb.w, fb.h);
}

void Host::selfTestDamage() {
    if (!field_) { std::printf("[dmgtest] no field\n"); return; }
    if (gameParty_.empty()) newGame();
    if (animDirty_) { buildAnimCells(); animDirty_ = false; }
    const FfMap* mp = field_->map();
    std::printf("[dmgtest] damage cells on map: %zu\n", damageCells_.size());
    if (damageCells_.empty() || !mp) return;
    static const int DXn[4] = {0,0,-1,1}, DYn[4] = {1,-1,0,0};
    int dc = -1, wn = -1, dir = -1;
    for (int c : damageCells_) {
        int cx = c % mp->w, cy = c / mp->w;
        for (int d = 0; d < 4; ++d) {
            int nx = cx + DXn[d], ny = cy + DYn[d];
            if (nx < 0 || ny < 0 || nx >= mp->w || ny >= mp->h) continue;
            int nc = ny * mp->w + nx;
            bool solid = !mp->pass.empty() && (mp->pass[nc] & 0x0f) == 0;
            if (solid || damageCells_.count(nc)) continue;
            wn = nc; dc = c;
            int odx = cx - nx, ody = cy - ny;
            dir = (ody > 0) ? 0 : (ody < 0) ? 1 : (odx < 0) ? 2 : 3;
            break;
        }
        if (dc >= 0) break;
    }
    if (dc < 0) { std::printf("[dmgtest] no reachable damage cell\n"); return; }
    field_->setPos(wn % mp->w, wn / mp->w); lastCell_ = -1; mode_ = Mode::Field; menuOpen_ = false;
    auto hpline = [&](const char* w){ std::printf("[dmgtest] %-6s HP:", w); for (auto& gm : gameParty_) std::printf(" %d", gm.hp); std::printf("\n"); };
    std::printf("[dmgtest] step from cell %d onto damage cell %d (dir %d)\n", wn, dc, dir);
    hpline("before");
    static const uint32_t BTN[4] = { BTN_DOWN, BTN_UP, BTN_LEFT, BTN_RIGHT };
    input_ = InputState{}; input_.held = BTN[dir];
    for (int g = 0; g < 300; ++g) {
        update(0.0);
        if (field_->col() == dc % mp->w && field_->row() == dc / mp->w && !field_->moving()) break;
    }
    hpline("after");
}

void Host::loadChipAnim(const std::string& dir) {
    chipAnim_ = load_chipanim(dir + "/data/chipanim.bin");
    chipFloor_ = load_chipfloor(dir + "/data/chipfloor.bin");
    size_t total = 0; for (auto& kv : chipAnim_) total += kv.second.size();
    size_t fl = 0; for (auto& kv : chipFloor_) fl += kv.second.size();
    std::printf("[FFSmith] chip anim: %zu tilesets / %zu chips; floor-attr chips: %zu\n", chipAnim_.size(), total, fl);
}

SDL_Texture* Host::slotSheet(int mc, int var, int& w, int& h) {
    if (mc < 0) return nullptr;
    int key = mc * 100 + var;
    auto it = sheets_.find(key);
    if (it != sheets_.end()) { if (it->second) SDL_QueryTexture(it->second, nullptr, nullptr, &w, &h); return it->second; }
    SDL_Texture* tex = nullptr; Texture t;
    for (int v : { var, 0 }) {
        char path[512]; std::snprintf(path, sizeof(path), "%s/tex/mc%d_%d.tex", bundleDir_.c_str(), mc, v);
        t = load_tex(path); if (t.valid()) break;
    }
    if (t.valid()) {
        tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, t.w, t.h);
        if (tex) { SDL_UpdateTexture(tex, nullptr, t.rgba.data(), t.w * 4); SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND); }
    }
    sheets_[key] = tex; if (tex) { w = t.w; h = t.h; }
    return tex;
}

void Host::buildAnimCells() {
    animCells_.clear(); damageCells_.clear(); lastCell_ = -1;
    if (!field_) return;
    const FfMap* mp = field_->map(); if (!mp) return;
    int ncells = mp->w * mp->h;
    for (int i = 0; i < ncells; ++i) {
        int word = 0, high = 0;                          // topmost non-zero chip across layers
        for (int L = (int)mp->layers.size() - 1; L >= 0; --L)
            if (i < (int)mp->layers[L].size()) { uint16_t wd = mp->layers[L][i]; if (wd) { word = wd; high = wd >> 8; break; } }
        if (!word) continue;
        int mc  = (high == 1 && mp->mc_slot1 >= 0) ? mp->mc_slot1 : mp->mc_slot0;
        int var = (high == 1 && mp->mc_slot1 >= 0) ? mp->var_slot1 : mp->var_slot0;
        int inner = word & 0xff;
        auto fit = chipFloor_.find(mc);                          // damage floor (only if walkable)?
        bool walkable = mp->pass.empty() || (i < (int)mp->pass.size() && (mp->pass[i] & 0x0f) != 0);
        if (walkable && fit != chipFloor_.end()) { auto fc = fit->second.find(inner); if (fc != fit->second.end() && (fc->second & 0x10)) damageCells_.insert(i); }
        auto mit = chipAnim_.find(mc); if (mit == chipAnim_.end()) continue;
        auto cit = mit->second.find(inner); if (cit == mit->second.end()) continue;
        animCells_.push_back({ i, mc, var, inner, cit->second.type, cit->second.frames, cit->second.speed });
    }
    std::printf("[FFSmith] field cells: %zu animated, %zu damage\n", animCells_.size(), damageCells_.size());
}

static int animFrameOf(int timer, int type, int frames, int speed) {
    if (frames <= 1) return 0;
    int div = speed > 0 ? speed * 8 : 8;                 // ~ticks/frame (exact speed table not decoded)
    int phase = timer / div;
    if (type == 1) { int period = (frames - 1) * 2; int t = ((phase % period) + period) % period; return t < frames ? t : period - t; }
    return ((phase % frames) + frames) % frames;
}

void Host::checkFieldHazard() {
    if (!field_ || damageCells_.empty()) return;
    const FfMap* mp = field_->map(); if (!mp) return;
    int cell = field_->row() * mp->w + field_->col();
    if (cell == lastCell_) return;                           // only on arrival at a new cell
    lastCell_ = cell;
    if (!damageCells_.count(cell)) return;
    int hit = 0;
    for (auto& gm : gameParty_) {
        if (gm.hp <= 0) continue;
        int mh = (gm.charIdx >= 0 && gm.charIdx < (int)chars_.size() && chars_[gm.charIdx].hp > 0) ? chars_[gm.charIdx].hp : 30;
        gm.hp = std::max(0, gm.hp - std::max(1, mh / 16));   // ~1/16 maxHP per step (exact amount not decoded)
        ++hit;
    }
    if (hit) std::printf("[FFSmith] damage floor @cell %d: %d members hurt\n", cell, hit);
}

void Host::setOverhead(const Texture& img) {
    if (overhead_tex_) { SDL_DestroyTexture(overhead_tex_); overhead_tex_ = nullptr; }
    if (!renderer_ || !img.valid()) return;       // no overhead layer -> nothing above sprites
    overhead_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
    if (overhead_tex_) { SDL_UpdateTexture(overhead_tex_, nullptr, img.rgba.data(), img.w * 4); SDL_SetTextureBlendMode(overhead_tex_, SDL_BLENDMODE_BLEND); }
}

void Host::setField(Field* f, const Texture& mapImg) {
    if (!ensureMapTexture(mapImg)) return;
    field_ = f;
    camInit_ = false;                       // snap the camera on map swaps
    if (field_) {
        if (!vmEnv_.rand) wireScriptEnv();
        field_->setScript(&scriptState_, &vmEnv_);
    }
    animDirty_ = true;
    SDL_RenderSetLogicalSize(renderer_, 0, 0);
}

// VM environment hooks: item ownership / party membership / RNG
// (FieldClass::GetReferenceItem / GetReferenceParty / GameClass::Rand).
void Host::wireScriptEnv() {
    vmEnv_.itemCount = [this](int id) {
        for (const auto& s : inventory_) if (s.id == id) return s.count;
        return 0;
    };
    vmEnv_.partyHas = [this](int memberId) {
        for (const auto& gm : gameParty_)
            if (gm.charIdx >= 0 && gm.charIdx < (int)chars_.size()
                && chars_[gm.charIdx].id == memberId) return true;
        return false;
    };
    vmEnv_.rand = [](int n) { return n > 0 ? (int)(std::rand() % n) : 0; };
    // GetReferenceBattle (c:135841) type 3: 1 = won, 2 = escaped, 0 = lost.
    vmEnv_.battleRef = [this](int type, long) -> long {
        return type == 3 ? lastBattleResult_ : 0;
    };
    // 0x2a SetFade: mode 0 = fade OUT (to color), mode 1+ = fade IN. Evidence:
    // every intro scene OPENS with mode 1 + duration and ENDS with mode 0 +
    // block-until-done right before its warp (m1 ev14/ev18, m200 ev2).
    vmEnv_.setFade = [this](int mode, int r, int g, int b, int ticks) {
        ++fadeGen_;                                                  // record that a script fade happened (warp recovery checks this)
        fadeR_ = (uint8_t)r; fadeG_ = (uint8_t)g; fadeB_ = (uint8_t)b;
        fadeTarget_ = (mode == 0) ? 255 : 0;
        if (fadeTarget_ == 255 && fadeAlpha_ == 0) fadeAlpha_ = 1;       // start ramp
        int dur = ticks > 0 ? ticks : 16;
        fadeStep_ = std::max(1, 255 / dur);
    };
    vmEnv_.findEvent = [this](int id) -> const Event* {
        if (field_)
            for (const auto& e : field_->map()->events) if (e.id == id) return &e;
        for (const auto& e : commonEvents_.events) if (e.id == id) return &e;
        return nullptr;
    };
}

// The shared CallEvent routine pool: field_constant.dat offset 0x8d names map
// 10000 (FieldClass::LoadCommonEvent); its event pack holds the common events
// (0x104 = move-map, 0x103 = spawn, 0x101 = story change, ...).
void Host::loadCommonEvents(const std::string& bundleDir) {
    commonEvents_ = load_ffmap(bundleDir + "/data/common_events.ffmap");
    if (commonEvents_.events.empty())
        std::printf("[FFSmith] no common-event pool (data/common_events.ffmap)\n");
    else
        std::printf("[FFSmith] common events: %zu routines\n", commonEvents_.events.size());
}

SDL_Texture* Host::spriteTex(int img, int var, int& w, int& h) {
    int key = img * 100 + var;
    auto it = sprites_.find(key);
    if (it != sprites_.end()) {
        if (it->second) SDL_QueryTexture(it->second, nullptr, nullptr, &w, &h);
        return it->second;
    }
    SDL_Texture* tex = nullptr;
    Texture t;
    for (int v : {var, 0}) {
        char path[512];
        std::snprintf(path, sizeof(path), "%s/sprites/fldchr%d_%d.tex", bundleDir_.c_str(), img, v);
        t = load_tex(path);
        if (t.valid()) break;
    }
    if (t.valid()) {
        tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, t.w, t.h);
        if (tex) {
            SDL_UpdateTexture(tex, nullptr, t.rgba.data(), t.w * 4);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        }
    }
    sprites_[key] = tex;
    if (tex) { w = t.w; h = t.h; }
    return tex;
}

// Cached monster battle sprite (tex/mon{group}_{variant}.tex = one static image;
// the FFD mon{N}_{M}.png "variants" are different monsters/recolors, not frames).
SDL_Texture* Host::monTex(int group, int variant, int& w, int& h) {
    int key = group * 1000 + variant;
    auto it = monSprites_.find(key);
    if (it != monSprites_.end()) { if (it->second) SDL_QueryTexture(it->second, nullptr, nullptr, &w, &h); return it->second; }
    char path[512]; std::snprintf(path, sizeof(path), "%s/tex/mon%d_%d.tex", bundleDir_.c_str(), group, variant);
    Texture t = load_tex(path);
    SDL_Texture* tex = nullptr;
    if (t.valid()) {
        tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, t.w, t.h);
        if (tex) { SDL_UpdateTexture(tex, nullptr, t.rgba.data(), t.w * 4); SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND); }
    }
    monSprites_[key] = tex;
    if (tex) { w = t.w; h = t.h; }
    return tex;
}

// Draw a character frame, feet-aligned on a tile at logical (lx,ly).
// field_anm character template (Jack-confirmed on fldchr1): 48x48 cells, origin
// (1,1), pitch 50.  ROW = facing: DOWN=y1, UP=y51, LEFT=y101, RIGHT=y101 (LEFT
// flipped horizontally).  COL = frame: idle=x1, walkA=x51, walkB=x101.
// Small NPC sheets fall back to the top-left cell.
bool Host::drawSprite(int img, int var, int facing, int animCol, int lx, int ly, int tile) {
    int tw = 0, th = 0;
    SDL_Texture* tex = spriteTex(img, var, tw, th);
    if (!tex) return false;
    auto git = spriteGeo_.find(img);                    // object sprites (doors/crystals/chests/effects): real frames + anchor
    if (git != spriteGeo_.end() && git->second.isObject && !git->second.frames.empty()) {
        const SpriteGeo& g = git->second;
        int nf = (int)g.frames.size();
        // Animate only isObject==2 (ambient effects: fire/flames); state sheets
        // (doors/chests/props, isObject==1) hold frame 0 and never auto-cycle.
        int fi = (g.isObject == 2 && nf > 1) ? (int)((SDL_GetTicks() / 140u) % (Uint32)nf) : 0;
        const SGFrame& fr = g.frames[fi];
        SDL_Texture* otex = tex;
        SDL_Rect osrc{ fr.x, fr.y, fr.w, fr.h };
        if (g.mode == 3) {                              // multifile: this frame is a sibling texture
            int mw = 0, mh = 0;
            SDL_Texture* mt = spriteTex(img, fi, mw, mh);
            if (mt) { otex = mt; osrc = SDL_Rect{ fr.x, fr.y, fr.w ? fr.w : mw, fr.h ? fr.h : mh }; }  // honour per-file crop (e.g. airship view cell)
        }
        int ax, ay;
        if (g.mode == 1) {                              // static: legacy authored anchor (sprite_grid.json contract)
            ax = lx + tile / 2 + g.px; ay = ly + tile + g.py;
        } else {                                        // grid/multifile: centre-bottom each frame by its own size
            ax = lx + tile / 2 - osrc.w / 2 + g.px; ay = ly + tile - osrc.h + g.py;
        }
        SDL_Rect odst{ ax, ay, osrc.w, osrc.h };
        SDL_RenderCopy(renderer_, otex, &osrc, &odst);
        return true;
    }
    int sx = 0, sy = 0, cw = 48, ch = 48;
    SDL_RendererFlip flip = SDL_FLIP_NONE;
    if (tw >= 149 && th >= 149) {
        static const int RY[4] = {1, 51, 101, 101};   // DOWN, UP, LEFT, RIGHT
        int f = (facing >= 0 && facing < 4) ? facing : 0;
        int c = (animCol >= 0 && animCol < 3) ? animCol : 0;
        sy = RY[f];
        sx = 1 + c * 50;
        if (f == FACE_RIGHT) flip = SDL_FLIP_HORIZONTAL;
    } else {
        cw = tw < 48 ? tw : 48;
        ch = th < 48 ? th : 48;
    }
    SDL_Rect src{ sx, sy, cw, ch };
    SDL_Rect dst{ lx + (tile - cw) / 2, ly + tile - ch, cw, ch };
    SDL_RenderCopyEx(renderer_, tex, &src, &dst, 0.0, nullptr, flip);
    return true;
}

bool Host::loadText(const std::string& dir, int bank) {
    messages_ = load_messages(dir + "/text/msg" + std::to_string(bank) + ".bin");
    if (!fontTex_) {                         // font is shared across banks -> load once
        Font f = load_font(dir + "/text/font.tex", dir + "/text/font.meta");
        if (f.valid() && renderer_) {
            fontTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                         f.atlas.w, f.atlas.h);
            if (fontTex_) {
                SDL_UpdateTexture(fontTex_, nullptr, f.atlas.rgba.data(), f.atlas.w * 4);
                SDL_SetTextureBlendMode(fontTex_, SDL_BLENDMODE_BLEND);
            }
            fcw_ = f.cw; fch_ = f.ch; fcols_ = f.cols; ffirst_ = f.first;
        }
    }
    std::printf("[FFSmith] text: bank %d, %zu messages, font %s\n",
                bank, messages_.size(), fontTex_ ? "loaded" : "missing");
    return !messages_.empty();
}

// Word-wrapped bitmap text from the baked atlas. Honors '\n'; wraps whole words
// at maxChars; unknown bytes (incl. multibyte UTF-8) render as '?'.
// Thousands-separated integer (e.g. 12345 -> "12,345"); for gil/EXP displays.
static std::string commafy(long v) {
    bool neg = v < 0; unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    std::string d = std::to_string(u), out;
    int c = 0;
    for (int i = (int)d.size() - 1; i >= 0; --i) { out.push_back(d[i]); if (++c % 3 == 0 && i > 0) out.push_back(','); }
    if (neg) out.push_back('-');
    std::reverse(out.begin(), out.end());
    return out;
}

void Host::drawText(int x, int y, const std::string& s, int maxChars, uint8_t r, uint8_t g, uint8_t b) {
    if (!fontTex_ || fcw_ <= 0 || maxChars < 1) return;
    auto pass = [&](int ox, int oy, uint8_t cr, uint8_t cg, uint8_t cb) {
        SDL_SetTextureColorMod(fontTex_, cr, cg, cb);
        int cx = 0, cy = 0;
        auto put = [&](unsigned char c) {
            if (c < (unsigned)ffirst_ || c >= (unsigned)(ffirst_ + 95)) c = '?';
            int gi = (int)c - ffirst_;
            SDL_Rect src{ (gi % fcols_) * fcw_, (gi / fcols_) * fch_, fcw_, fch_ };
            SDL_Rect dst{ ox + cx * fcw_, oy + cy * fch_, fcw_, fch_ };
            SDL_RenderCopy(renderer_, fontTex_, &src, &dst);
        };
        size_t i = 0;
        while (i < s.size()) {
            char ch = s[i];
            if (ch == '\n') { cx = 0; ++cy; ++i; continue; }
            if (ch == ' ')  { if (++cx >= maxChars) { cx = 0; ++cy; } ++i; continue; }
            size_t j = i;
            while (j < s.size() && s[j] != ' ' && s[j] != '\n') ++j;
            int wl = (int)(j - i);
            if (cx + wl > maxChars && wl <= maxChars) { cx = 0; ++cy; }
            for (size_t k = i; k < j; ++k) {
                if (cx >= maxChars) { cx = 0; ++cy; }
                put((unsigned char)s[k]); ++cx;
            }
            i = j;
        }
    };
    if (textShadow_) pass(x + 1, y + 1, 12, 12, 20);   // drop shadow under the colored text
    pass(x, y, r, g, b);
}

// Colored HP/MP gauge.  HP fades green -> yellow -> red as it drains; MP is blue.
void Host::drawGauge(int x, int y, int w, int h, int cur, int maxv, bool mp) {
    if (w <= 0 || h <= 0) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 18, 20, 30, 220);
    SDL_Rect bg{ x, y, w, h }; SDL_RenderFillRect(renderer_, &bg);
    int c = cur < 0 ? 0 : (cur > maxv ? maxv : cur);
    int fw = maxv > 0 ? (w - 2) * c / maxv : 0;
    int r, g, b;
    if (mp) { r = 80; g = 140; b = 235; }
    else { double t = maxv > 0 ? (double)c / maxv : 0.0;
        if (t > 0.5)      { r = 80;  g = 205; b = 90; }
        else if (t > 0.25){ r = 235; g = 200; b = 70; }
        else              { r = 225; g = 70;  b = 60; } }
    if (fw > 0) { SDL_SetRenderDrawColor(renderer_, (Uint8)r, (Uint8)g, (Uint8)b, 255);
                  SDL_Rect f{ x + 1, y + 1, fw, h - 2 }; SDL_RenderFillRect(renderer_, &f); }
    SDL_SetRenderDrawColor(renderer_, 150, 165, 200, 180);
    SDL_RenderDrawRect(renderer_, &bg);
}

// FFD window skin -- faithful reproduction of GameClass::DrawWindow
// (libjniproxy.so_new.c 153133-153153): a 3-stop vertical gradient drawn as two
// half-rects -- top RGB(63,69,134) -> middle RGB(42,43,102) over the upper half,
// then middle -> bottom RGB(75,78,122) over the lower half -- at the given alpha
// (the game uses 0xa0), finished with a light frame.  We approximate the gradient
// with per-row lines and DrawWindowFrame with a light border.
void Host::drawWindow(int x, int y, int w, int h, int alpha) {
    if (w <= 0 || h <= 0) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    auto lerp = [](int a, int b, double t) { return (int)(a + (b - a) * t + 0.5); };
    const WinColor& wc = kWinColors[winColorIdx_ % kWinColorN];
    int a = std::min(255, std::max(0, alpha * winOpacity_ / 230));   // global opacity scales the per-call alpha
    const int half = h / 2;
    for (int row = 0; row < h; ++row) {
        int r, g, b;
        if (row < half) { double t = half > 0 ? (double)row / half : 0.0;
            r = lerp(wc.tr, wc.mr, t); g = lerp(wc.tg, wc.mg, t); b = lerp(wc.tb, wc.mb, t); }
        else { int den = h - half; double t = den > 0 ? (double)(row - half) / den : 0.0;
            r = lerp(wc.mr, wc.br, t); g = lerp(wc.mg, wc.bg, t); b = lerp(wc.mb, wc.bb, t); }
        SDL_SetRenderDrawColor(renderer_, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a);
        SDL_RenderDrawLine(renderer_, x + 1, y + row, x + w - 2, y + row);
    }
    SDL_SetRenderDrawColor(renderer_, 206, 220, 250, 255);   // light frame (DrawWindowFrame)
    SDL_Rect fr{ x, y, w, h }; SDL_RenderDrawRect(renderer_, &fr);
    SDL_SetRenderDrawColor(renderer_, 70, 88, 150, 255);     // subtle inner shade line
    SDL_Rect ir{ x + 1, y + 1, w - 2, h - 2 }; SDL_RenderDrawRect(renderer_, &ir);
}

static uint32_t keyToButton(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP: case SDLK_w: return BTN_UP;
        case SDLK_DOWN: case SDLK_s: return BTN_DOWN;
        case SDLK_LEFT: case SDLK_a: return BTN_LEFT;
        case SDLK_RIGHT: case SDLK_d: return BTN_RIGHT;
        case SDLK_z: case SDLK_SPACE: return BTN_CONFIRM;
        case SDLK_RETURN: case SDLK_TAB: return BTN_MENU;
        case SDLK_x: case SDLK_BACKSPACE: return BTN_CANCEL;
        case SDLK_q: return BTN_L;
        case SDLK_e: return BTN_R;
        default: return BTN_NONE;
    }
}

void Host::pumpEvents() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT: running_ = false; break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_ESCAPE) { running_ = false; break; }
                if (ev.key.keysym.sym == SDLK_F1) { mode_ = (mode_ == Mode::Field) ? Mode::Debug : Mode::Field; break; }
                if (ev.key.keysym.sym == SDLK_F2 && field_) { field_->setNoClip(!field_->noClip()); break; }
                if (ev.key.keysym.sym == SDLK_F3) { overlayOn_ = !overlayOn_; break; }
                if (ev.key.keysym.sym == SDLK_F4) { hudOn_ = !hudOn_; break; }
                if (ev.key.keysym.sym == SDLK_F5) { saveReq_ = true; break; }
                if (ev.key.keysym.sym == SDLK_F9) { loadReq_ = true; break; }
                if (ev.key.keysym.sym == SDLK_b && mode_ == Mode::Field && !monsters_.empty()) { startBattle(-1); break; }
                if (ev.key.keysym.sym == SDLK_MINUS) { cfg_.scale = std::max(1, cfg_.scale - 1); dbgScale_ = cfg_.scale; break; }
                if (ev.key.keysym.sym == SDLK_EQUALS || ev.key.keysym.sym == SDLK_PLUS) { cfg_.scale = std::min(8, cfg_.scale + 1); dbgScale_ = cfg_.scale; break; }
                if (!ev.key.repeat) raw_held_ |= keyToButton(ev.key.keysym.sym);
                break;
            case SDL_KEYUP: raw_held_ &= ~keyToButton(ev.key.keysym.sym); break;
            default: break;
        }
    }
}

void Host::stepInput() {
    input_.held = raw_held_;
    input_.pressed = raw_held_ & ~held_prev_;
    input_.released = held_prev_ & ~raw_held_;
    held_prev_ = raw_held_;
}

void Host::updateAudio() {
    if (!audio_.ok()) return;
    // BGM follows the mode; playBgm() is a no-op when the id is already playing,
    // so this self-corrects on every map/mode change. Battle BGM is set
    // explicitly in startBattle() and is left alone here.
    if (mode_ == Mode::Field && field_ && field_->map())
        audio_.playBgm(field_->map()->field_bgm);
    else if (mode_ == Mode::Title || mode_ == Mode::Intro)
        audio_.playBgm(titleBgm_);
    else if (mode_ == Mode::Debug)
        audio_.stopBgm();
}

void Host::update(double /*dt*/) {
    ++animTimer_;
    updateAudio();
    if (fadeAlpha_ != fadeTarget_) {                       // 0x2a fade ramp
        if (fadeAlpha_ < fadeTarget_) fadeAlpha_ = std::min(fadeTarget_, fadeAlpha_ + fadeStep_);
        else fadeAlpha_ = std::max(fadeTarget_, fadeAlpha_ - fadeStep_);
    }
    if (mode_ == Mode::Debug) {
        updateDebug(input_);
    } else if (mode_ == Mode::Battle) {
        updateBattle(input_);
    } else if (mode_ == Mode::Title) {
        ++blink_;
        const int n = 3;
        if (input_.pressed & BTN_UP)   titleSel_ = (titleSel_ + n - 1) % n;
        if (input_.pressed & BTN_DOWN) titleSel_ = (titleSel_ + 1) % n;
        if (input_.pressed & BTN_CONFIRM) {
            audio_.playSe(1);                                        // decide
            if (titleSel_ == 0) {                                    // New Game -> intro cinematics
                newGame(); introState_ = 0; mode_ = Mode::Intro;
            } else if (titleSel_ == 1) {                            // Continue
                if (hasSave_) loadReq_ = true;
            } else {                                                // Debug Menu
                mode_ = Mode::Debug;
            }
            blink_ = 0;
        }
    } else if (mode_ == Mode::Intro) {
        ++blink_;
        if (input_.pressed & (BTN_CONFIRM | BTN_MENU)) {
            ++introState_; blink_ = 0;
            if (introState_ >= 3 && !startMap_.empty()) {           // intro done -> load the opening field
                debugSelectMap(startMap_);
                int heroImg = 13;     // lead's field sprite (Sol = chpk 13); from chars.bin when baked
                if (!gameParty_.empty() && gameParty_[0].charIdx >= 0 && gameParty_[0].charIdx < (int)chars_.size() && chars_[gameParty_[0].charIdx].chpk > 0)
                    heroImg = chars_[gameParty_[0].charIdx].chpk;
                dbgSprIdx_ = 0;
                for (int i = 0; i < (int)dbgSprites_.size(); ++i) if (dbgSprites_[i] == heroImg) { dbgSprIdx_ = i; break; }
                dbgX_ = startX_; dbgY_ = startY_;   // real New Game spawn (boot scenario 0)
                dbgFacing_ = 0; dbgNoclip_ = false; dbgStart_ = true;
            }
        }
    } else if (menuOpen_) {
        updateMenu(input_);
    } else if (field_) {
        if (input_.pressed & BTN_MENU) { menuOpen_ = true; menuCursor_ = 0; }
        else {
            // message typewriter: reveal dialogue text over time; the first confirm
            // completes the reveal instead of advancing to the next line.
            if (field_->inDialogue() && !field_->choiceActive()) {
                int id = field_->dialogueMsg();
                auto it = messages_.find(id);
                int full = (it != messages_.end()) ? (int)it->second.size() : 0;
                if (id != msgRevealId_) { msgRevealId_ = id; msgReveal_ = (msgSpeed_ == 0) ? full : 0; }
                if (msgReveal_ < full) {
                    if (input_.pressed & BTN_CONFIRM) { msgReveal_ = full; input_.pressed &= ~BTN_CONFIRM; }
                    else msgReveal_ = std::min(full, msgReveal_ + (msgSpeed_ == 2 ? 1 : 2));
                }
            } else msgRevealId_ = -2;
            field_->update(input_);
            checkFieldHazard();
            if (encountersOn_) maybeRandomEncounter();
        }
        // a script hit 0x50: show its text first, then hand off to battle
        if (!field_->inDialogue() && field_->encounterPending())
            startFormationBattle(field_->startEncounter());
    }
    ++tick_count_;
    if (cfg_.max_frames >= 0 && static_cast<int>(tick_count_) >= cfg_.max_frames) running_ = false;
}

void Host::updateMenu(const InputState& in) {
    if (menuPage_ == 0) {                                   // root list
        if (in.pressed & BTN_UP)   menuCursor_ = (menuCursor_ + kMenuN - 1) % kMenuN;
        if (in.pressed & BTN_DOWN) menuCursor_ = (menuCursor_ + 1) % kMenuN;
        if (in.pressed & (BTN_CANCEL | BTN_MENU)) menuOpen_ = false;
        if (in.pressed & BTN_CONFIRM) {
            switch (menuCursor_) {
                case 0: menuPage_ = 1; pageCursor_ = 0; pageScroll_ = 0; menuMsg_.clear(); break;  // Item
                case 1: menuPage_ = 5; break;                                     // Magic  (stub)
                case 2: menuPage_ = 2; pageChar_ = 0; equipSlot_ = 0; equipSub_ = 0; menuMsg_.clear(); break;  // Equip
                case 3: menuPage_ = 6; break;                                     // Job    (stub)
                case 4: menuPage_ = 3; pageChar_ = 0; break;                      // Status
                case 5: menuPage_ = 7; formCursor_ = 0; formHeld_ = -1; formMode_ = 0; break;  // Formation
                case 6: menuPage_ = 4; configCursor_ = 0; break;                  // Config (display)
                case 7: saveReq_ = true; menuOpen_ = false; break;                // Save
                case 8: running_ = false; break;                                  // Quit
                default: break;
            }
        }
    } else if (menuPage_ == 1) {                            // Item list (inventory)
        int n = (int)inventory_.size();
        if (n > 0) {
            if (in.pressed & BTN_UP)      { pageCursor_ = (pageCursor_ + n - 1) % n; menuMsg_.clear(); }
            if (in.pressed & BTN_DOWN)    { pageCursor_ = (pageCursor_ + 1) % n;     menuMsg_.clear(); }
            if (in.pressed & BTN_CONFIRM) useItem(pageCursor_);
        }
        if (in.pressed & (BTN_CANCEL | BTN_MENU)) { menuPage_ = 0; menuMsg_.clear(); }
    } else if (menuPage_ == 5 || menuPage_ == 6) {          // Magic / Job -- stub pages
        if (in.pressed & (BTN_CANCEL | BTN_MENU | BTN_CONFIRM)) menuPage_ = 0;
    } else if (menuPage_ == 7) {                            // Formation (active side, up to 5)
        if (formMode_ == 1) {                              // add-member picker
            std::vector<int> pool;
            for (int ci = 0; ci < (int)chars_.size(); ++ci) if (!charInAnyParty(ci)) pool.push_back(ci);
            int np = (int)pool.size();
            if (np > 0) {
                if (in.pressed & BTN_UP)   formPick_ = (formPick_ + np - 1) % np;
                if (in.pressed & BTN_DOWN) formPick_ = (formPick_ + 1) % np;
                if ((in.pressed & BTN_CONFIRM) && (int)gameParty_.size() < kPartyMax) {
                    gameParty_.push_back(makeMember(pool[formPick_])); commitActiveParty(); formMode_ = 0;
                }
            }
            if (in.pressed & (BTN_CANCEL | BTN_MENU)) formMode_ = 0;
            return;
        }
        if (in.pressed & (BTN_LEFT | BTN_RIGHT)) { switchSide(); formCursor_ = 0; formHeld_ = -1; }
        if (in.pressed & BTN_UP)   formCursor_ = (formCursor_ + kPartyMax - 1) % kPartyMax;
        if (in.pressed & BTN_DOWN) formCursor_ = (formCursor_ + 1) % kPartyMax;
        if (in.pressed & BTN_R) {                          // remove member under cursor
            if (formCursor_ < (int)gameParty_.size()) { gameParty_.erase(gameParty_.begin() + formCursor_);
                commitActiveParty(); formHeld_ = -1; }
        }
        if (in.pressed & BTN_CONFIRM) {
            bool occupied = formCursor_ < (int)gameParty_.size();
            if (formHeld_ >= 0) {                          // place / swap the held member
                if (occupied && formHeld_ < (int)gameParty_.size())
                    std::swap(gameParty_[formHeld_], gameParty_[formCursor_]);
                commitActiveParty(); formHeld_ = -1;
            } else if (occupied) {
                formHeld_ = formCursor_;                   // pick up to reorder
            } else if ((int)gameParty_.size() < kPartyMax) {
                formMode_ = 1; formPick_ = 0;              // empty slot -> add picker
            }
        }
        if (in.pressed & (BTN_CANCEL | BTN_MENU)) { if (formHeld_ >= 0) formHeld_ = -1; else menuPage_ = 0; }
    } else if (menuPage_ == 4) {                            // Config: settings list
        const int ROWS = 5;
        if (in.pressed & BTN_UP)   configCursor_ = (configCursor_ + ROWS - 1) % ROWS;
        if (in.pressed & BTN_DOWN) configCursor_ = (configCursor_ + 1) % ROWS;
        int d = (in.pressed & BTN_RIGHT) ? 1 : (in.pressed & BTN_LEFT) ? -1 : 0;
        bool conf = (in.pressed & BTN_CONFIRM) != 0;
        switch (configCursor_) {
            case 0: if (d) { int cur = 0; for (int i = 0; i < kAspectN; ++i) if (kAspects[i].w == cfg_.aspectW && kAspects[i].h == cfg_.aspectH) cur = i;
                        cur = (cur + d + kAspectN) % kAspectN; setAspect(kAspects[cur].w, kAspects[cur].h); } break;
            case 1: if (d) winOpacity_ = std::max(80, std::min(255, winOpacity_ + d * 10)); break;   // opacity
            case 2: if (d || conf) winColorIdx_ = (winColorIdx_ + (d ? d : 1) + kWinColorN) % kWinColorN; break;  // colour
            case 3: if (d || conf) textShadow_ = !textShadow_; break;                                // shadow
            case 4: if (d || conf) msgSpeed_ = (msgSpeed_ + (d ? d : 1) + 3) % 3; break;             // message speed
        }
        if (in.pressed & (BTN_CANCEL | BTN_MENU)) menuPage_ = 0;
    } else if (menuPage_ == 3) {                            // Status: cycle party member
        int n = (int)gameParty_.size();
        if (n > 0) {
            if (in.pressed & (BTN_DOWN | BTN_RIGHT)) pageChar_ = (pageChar_ + 1) % n;
            if (in.pressed & (BTN_UP | BTN_LEFT))    pageChar_ = (pageChar_ + n - 1) % n;
        }
        if (in.pressed & (BTN_CANCEL | BTN_MENU)) menuPage_ = 0;
    } else {                                                // Equip: slot cursor + candidate list + swap
        int n = (int)gameParty_.size();
        if (equipSub_ == 0) {
            if (in.pressed & BTN_DOWN) { equipSlot_ = (equipSlot_ + 1) % 6; menuMsg_.clear(); }
            if (in.pressed & BTN_UP)   { equipSlot_ = (equipSlot_ + 5) % 6; menuMsg_.clear(); }
            if (n > 0 && (in.pressed & BTN_RIGHT)) { pageChar_ = (pageChar_ + 1) % n; equipSlot_ = 0; menuMsg_.clear(); }
            if (n > 0 && (in.pressed & BTN_LEFT))  { pageChar_ = (pageChar_ + n - 1) % n; equipSlot_ = 0; menuMsg_.clear(); }
            if (in.pressed & BTN_CONFIRM) { buildEquipCandidates(equipSlot_); equipSub_ = 1; equipPick_ = 0; menuMsg_.clear(); }
            if (in.pressed & (BTN_CANCEL | BTN_MENU)) menuPage_ = 0;
        } else {
            int rows = (int)equipCand_.size() + 1;          // + [Remove]
            if (in.pressed & BTN_DOWN) equipPick_ = (equipPick_ + 1) % rows;
            if (in.pressed & BTN_UP)   equipPick_ = (equipPick_ + rows - 1) % rows;
            if (in.pressed & BTN_CONFIRM) { swapEquip(equipSlot_, equipPick_); equipSub_ = 0; }
            if (in.pressed & BTN_CANCEL) { equipSub_ = 0; menuMsg_.clear(); }
        }
    }
}

void Host::render() {
    if (mode_ == Mode::Debug) { renderDebug(); SDL_RenderPresent(renderer_); return; }
    if (mode_ == Mode::Battle) { renderBattle(); SDL_RenderPresent(renderer_); return; }
    if (mode_ == Mode::Title) { renderTitle(); SDL_RenderPresent(renderer_); return; }
    if (mode_ == Mode::Intro) { renderIntro(); SDL_RenderPresent(renderer_); return; }
    if (field_ && map_tex_) {
        int winW = 0, winH = 0;
        SDL_GetWindowSize(window_, &winW, &winH);
        const int sc = cfg_.scale < 1 ? 1 : cfg_.scale;
        int vw = winW / sc; if (vw < 16) vw = 16;
        int vh = winH / sc; if (vh < 16) vh = 16;
        SDL_RenderSetScale(renderer_, (float)sc, (float)sc);

        const int tile = field_->tile();
        const int px = field_->pixelX(), py = field_->pixelY();
        // Camera target: the look-actor (0x1b CameraFollow) or the player.
        int tx = px, ty = py;
        field_->lookTargetPixel(tx, ty);
        int wantX = tx + tile / 2 - vw / 2, wantY = ty + tile / 2 - vh / 2;
        // Smooth pan toward the target (SetLookChara re-target + MoveScroll glide).
        if (!camInit_) { camPX_ = wantX; camPY_ = wantY; camInit_ = true; }
        auto ease = [](int cur, int want) {
            int d = want - cur;
            if (d == 0) return cur;
            int step = std::max(2, std::abs(d) / 12);
            if (std::abs(d) <= step) return want;
            return cur + (d > 0 ? step : -step);
        };
        camPX_ = ease(camPX_, wantX); camPY_ = ease(camPY_, wantY);
        int camX = camPX_, camY = camPY_;
        int offX = 0, offY = 0;
        if (mapW_ <= vw) { camX = 0; offX = (vw - mapW_) / 2; }
        else { if (camX < 0) camX = 0; if (camX > mapW_ - vw) camX = mapW_ - vw; }
        if (mapH_ <= vh) { camY = 0; offY = (vh - mapH_) / 2; }
        else { if (camY < 0) camY = 0; if (camY > mapH_ - vh) camY = mapH_ - vh; }

        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        SDL_Rect src{ camX, camY, (mapW_ < vw ? mapW_ : vw), (mapH_ < vh ? mapH_ : vh) };
        SDL_Rect dst{ offX, offY, src.w, src.h };
        SDL_RenderCopy(renderer_, map_tex_, &src, &dst);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

        // Animated tiles: overdraw the current frame of each animated cell (under sprites).
        if (animDirty_) { buildAnimCells(); animDirty_ = false; }
        if (!animCells_.empty()) {
            const FfMap* mp = field_->map();
            for (const auto& a : animCells_) {
                int cx = a.idx % mp->w, cy = a.idx / mp->w;
                int dxp = offX + cx * tile - camX, dyp = offY + cy * tile - camY;
                if (dxp + tile <= 0 || dyp + tile <= 0 || dxp >= vw || dyp >= vh) continue;
                int frame = animFrameOf(animTimer_, a.type, a.frames, a.speed);
                int inner = a.baseInner + frame;
                int sw = 0, sh = 0; SDL_Texture* sheet = slotSheet(a.mc, a.var, sw, sh);
                if (!sheet) continue;
                int tcols = tile > 0 ? sw / tile : 0; if (tcols <= 0) continue;
                SDL_Rect ss{ (inner % tcols) * tile, (inner / tcols) * tile, tile, tile };
                SDL_Rect dd{ dxp, dyp, tile, tile };
                SDL_RenderCopy(renderer_, sheet, &ss, &dd);
            }
        }

        // NPC sprites — drawn from ACTOR state (position/facing/walk-anim/alpha),
        // so scripted movement (0x68) and fades render live.
        for (const auto& a : field_->actors()) {
            if (a.evIndex < 0 || a.evIndex >= (int)field_->map()->events.size()) continue;
            const Event& e = field_->map()->events[a.evIndex];
            if (!field_->appears(e)) continue;   // CheckEventAppear gate
            if (!a.visible || a.alpha <= 0) continue;
            int lx = offX + a.pixelX(tile) - camX, ly = offY + a.pixelY(tile) - camY;
            if (e.img > 0) {
                int tw = 0, th = 0;
                SDL_Texture* tex = spriteTex(e.img, e.var, tw, th);
                if (tex && a.alpha < 255) SDL_SetTextureAlphaMod(tex, (Uint8)a.alpha);
                bool ok = drawSprite(e.img, e.var, a.facing, a.animCol(tile), lx, ly, tile);
                if (tex) SDL_SetTextureAlphaMod(tex, 255);
                if (!ok) {
                    SDL_SetRenderDrawColor(renderer_, 80, 170, 255, 200);
                    SDL_Rect er{ lx, ly, tile, tile }; SDL_RenderFillRect(renderer_, &er);
                }
            } else if (!e.scripts.empty() && overlayOn_) {
                SDL_SetRenderDrawColor(renderer_, 120, 255, 120, 70);
                SDL_Rect er{ lx, ly, tile, tile }; SDL_RenderFillRect(renderer_, &er);
            }
        }

        // player sprite (fallback to yellow marker if no sprite)
        const int sx = offX + px - camX, sy = offY + py - camY;
        if (playerImg_ < 0 || !drawSprite(playerImg_, playerVar_, field_->facing(), field_->animCol(), sx, sy, tile)) {
            SDL_SetRenderDrawColor(renderer_, 255, 230, 40, 200);
            SDL_Rect pr{ sx, sy, tile, tile }; SDL_RenderFillRect(renderer_, &pr);
            SDL_SetRenderDrawColor(renderer_, 20, 20, 20, 230);
            const int t = tile / 4; SDL_Rect fr;
            switch (field_->facing()) {
                case FACE_DOWN:  fr = { sx + tile/2 - t/2, sy + tile - t, t, t }; break;
                case FACE_UP:    fr = { sx + tile/2 - t/2, sy, t, t }; break;
                case FACE_LEFT:  fr = { sx, sy + tile/2 - t/2, t, t }; break;
                default:         fr = { sx + tile - t, sy + tile/2 - t/2, t, t }; break;
            }
            SDL_RenderFillRect(renderer_, &fr);
        }

        // Overhead layers (1+): drawn ABOVE the player so it passes behind canopies/roofs.
        if (overhead_tex_) SDL_RenderCopy(renderer_, overhead_tex_, &src, &dst);

        // Screen fade (0x2a SetFade; Host-owned, persists across warps)
        if (fadeAlpha_ > 0) {
            SDL_SetRenderDrawColor(renderer_, fadeR_, fadeG_, fadeB_, (Uint8)fadeAlpha_);
            SDL_Rect full{ 0, 0, vw, vh }; SDL_RenderFillRect(renderer_, &full);
        }

        if (overlayOn_) {
            const FfMap* mp = field_->map();
            for (int r = 0; r < mp->h; ++r)
                for (int c = 0; c < mp->w; ++c) {
                    if (mp->pass.empty() || (mp->pass[(size_t)r * mp->w + c] & 0x0f) != 0) continue;
                    SDL_SetRenderDrawColor(renderer_, 230, 40, 40, 90);
                    SDL_Rect rr{ offX + c*tile - camX, offY + r*tile - camY, tile, tile };
                    SDL_RenderFillRect(renderer_, &rr);
                }
            for (const auto& e : mp->events)
                if (e.img > 0 && (e.boot == 0 || e.boot == 1)) {
                    SDL_SetRenderDrawColor(renderer_, 60, 120, 255, 90);
                    SDL_Rect rr{ offX + e.x*tile - camX, offY + e.y*tile - camY, tile, tile };
                    SDL_RenderFillRect(renderer_, &rr);
                }
        }
        if (hudOn_ && fcw_ > 0) {
            const char* fn = field_->facing()==0?"down":field_->facing()==1?"up":field_->facing()==2?"left":"right";
            std::string hud = mapKey_ + " (" + std::to_string(field_->col()) + "," + std::to_string(field_->row())
                            + ") " + fn + (field_->noClip() ? "  NOCLIP" : "");
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 175);
            SDL_Rect hb{ 2, 2, (int)hud.size()*fcw_ + 6, fch_ + 4 }; SDL_RenderFillRect(renderer_, &hb);
            drawText(5, 4, hud, 80, 180, 255, 180);
        }
        if (field_->inDialogue()) {
            const int boxH = 68, by = vh - boxH - 4, bx = 6, bw = vw - 12;
            drawWindow(bx, by, bw, boxH, 225);   // FFD message window skin
            const int maxChars = fcw_ > 0 ? (bw - 12) / fcw_ : 30;
            if (field_->choiceActive()) {
                // 0x3c choice menu: each option value is a message id (the choice line)
                const auto& opts = field_->choiceOptions();
                int yy = by + 6;
                for (size_t k = 0; k < opts.size() && yy < by + boxH - fch_; ++k) {
                    auto it = messages_.find(opts[k].first);
                    std::string line = (it != messages_.end()) ? it->second
                                       : ("Option " + std::to_string(k + 1));
                    for (auto& c : line) if (c == '\n') c = ' ';
                    bool sel = ((int)k == field_->choiceSel());
                    drawText(bx + 6, yy, (sel ? "> " : "  ") + line, maxChars,
                             sel ? 255 : 170, sel ? 240 : 170, sel ? 120 : 170);
                    yy += fch_ + 2;
                }
            } else {
                const int id = field_->dialogueMsg();
                auto it = messages_.find(id);
                std::string txt = (it != messages_.end()) ? it->second
                                                           : ("[msg " + std::to_string(id) + "]");
                if (id == msgRevealId_ && msgReveal_ < (int)txt.size())   // typewriter reveal
                    txt = txt.substr(0, std::max(0, msgReveal_));
                drawText(bx + 6, by + 6, txt, maxChars, 250, 247, 235);   // warm dialogue tint (vs cool narration)
            }
        }
        if (field_->inSentence()) {
            // op 0x01 ScriptSentence: full-screen telop over the scene (FieldClass
            // Sentence system, libjniproxy DrawSentence) -- NOT a window. We dim the
            // backdrop to a deep blue (the classic FF opening narration) and stack
            // the accumulated lines centred, with a continue prompt.
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_, 12, 18, 64, 220);
            SDL_Rect full{ 0, 0, vw, vh }; SDL_RenderFillRect(renderer_, &full);
            const int maxC = fcw_ > 0 ? (vw - 8) / fcw_ : 30;
            int y = vh / 3;
            for (int sid : field_->sentenceLines()) {
                auto it = messages_.find(sid);
                std::string txt = (it != messages_.end()) ? it->second : ("[msg " + std::to_string(sid) + "]");
                size_t i = 0;
                while (i <= txt.size()) {
                    size_t nl = txt.find('\n', i);
                    std::string ln = txt.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
                    int lx = vw / 2 - (int)ln.size() * fcw_ / 2; if (lx < 4) lx = 4;
                    drawText(lx, y, ln, maxC, 226, 232, 250);
                    y += fch_ + 4;
                    if (nl == std::string::npos) break;
                    i = nl + 1;
                }
            }
            drawText(vw / 2 - 3 * fcw_, vh - fch_ * 3, "[Z]", 6, 200, 210, 255);
        }
        if (menuOpen_) renderMenu(vw, vh);
        SDL_RenderPresent(renderer_);
        return;
    }
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

bool Host::loadTitle(const std::string& bundleDir) {
    Texture t = load_tex(bundleDir + "/ui/title.tex");
    if (!t.valid() || !renderer_) return false;
    if (titleTex_) SDL_DestroyTexture(titleTex_);
    if (btlbgTex_) SDL_DestroyTexture(btlbgTex_);
    titleTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, t.w, t.h);
    if (titleTex_) {
        SDL_UpdateTexture(titleTex_, nullptr, t.rgba.data(), t.w * 4);
        SDL_SetTextureBlendMode(titleTex_, SDL_BLENDMODE_BLEND);
        titleW_ = t.w; titleH_ = t.h;
    }
    return titleTex_ != nullptr;
}

void Host::loadIntro(const std::string& dir) {
    introProl_.clear(); introChap_ = "Prologue";
    FILE* f = std::fopen((dir + "/data/intro.bin").c_str(), "rb");
    if (!f) return;
    char mag[4] = {0,0,0,0};
    bool ok = (std::fread(mag, 1, 4, f) == 4) && mag[0] == 'F' && mag[1] == 'I' && mag[2] == 'N' && mag[3] == 'T';
    if (ok) {
        uint16_t n = 0;
        if (std::fread(&n, 2, 1, f) == 1 && n > 0) {
            std::string str(n, 0);
            std::fread(&str[0], 1, n, f);
            introProl_ = str;
        }
        if (std::fread(&n, 2, 1, f) == 1 && n > 0) {
            std::string str(n, 0);
            std::fread(&str[0], 1, n, f);
            introChap_ = str;
        }
    }
    std::fclose(f);
}

void Host::renderIntro() {
    int winW = 0, winH = 0; SDL_GetWindowSize(window_, &winW, &winH);
    const int sc = cfg_.scale < 1 ? 1 : cfg_.scale;
    int vw = winW / sc, vh = winH / sc; if (vw < 16) vw = 16; if (vh < 16) vh = 16;
    SDL_RenderSetScale(renderer_, (float)sc, (float)sc);
    int st = introState_ < 0 ? 0 : (introState_ > 2 ? 2 : introState_);
    if (st == 0) {                                              // prologue text crawl
        SDL_SetRenderDrawColor(renderer_, 18, 24, 150, 255); SDL_RenderClear(renderer_);
        int y = vh / 4; size_t i = 0;
        while (i <= introProl_.size()) {
            size_t nl = introProl_.find('\n', i);
            std::string line = introProl_.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
            int lx = vw / 2 - (int)line.size() * fcw_ / 2;
            drawText(lx, y, line, 120, 238, 238, 248);
            y += fch_ + 5;
            if (nl == std::string::npos) break;
            i = nl + 1;
        }
        if (((blink_ / 24) & 1) == 0) drawText(vw / 2 - 3 * fcw_, vh - fch_ * 3, "[Z]", 6, 200, 210, 255);
    } else if (st == 1) {                                       // FF logo card
        SDL_SetRenderDrawColor(renderer_, 245, 244, 236, 255); SDL_RenderClear(renderer_);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        if (titleTex_ && titleW_ > 0) {
            double s = std::min((double)vw * 0.72 / titleW_, (double)vh * 0.40 / titleH_); if (s <= 0) s = 1.0;
            int dw = (int)(titleW_ * s), dh = (int)(titleH_ * s);
            SDL_Rect dst{ (vw - dw) / 2, (vh - dh) / 2, dw, dh }; SDL_RenderCopy(renderer_, titleTex_, nullptr, &dst);
        }
    } else {                                                    // "Prologue" chapter card
        SDL_SetRenderDrawColor(renderer_, 245, 244, 236, 255); SDL_RenderClear(renderer_);
        int lx = vw / 2 - (int)introChap_.size() * fcw_ / 2;
        drawText(lx, vh / 2 - fch_, introChap_, 40, 60, 50, 45);
        SDL_SetRenderDrawColor(renderer_, 150, 140, 120, 255);
        SDL_Rect fl{ vw / 2 - 40, vh / 2 + fch_, 80, 2 }; SDL_RenderFillRect(renderer_, &fl);
    }
}

void Host::renderTitle() {
    int winW = 0, winH = 0; SDL_GetWindowSize(window_, &winW, &winH);
    const int sc = cfg_.scale < 1 ? 1 : cfg_.scale;
    int vw = winW / sc; if (vw < 16) vw = 16;
    int vh = winH / sc; if (vh < 16) vh = 16;
    SDL_RenderSetScale(renderer_, (float)sc, (float)sc);
    SDL_SetRenderDrawColor(renderer_, 8, 10, 28, 255);
    SDL_RenderClear(renderer_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    if (titleTex_ && titleW_ > 0) {
        double s = std::min((double)vw * 0.82 / titleW_, (double)vh * 0.55 / titleH_);
        if (s <= 0.0) s = 1.0;
        int dw = (int)(titleW_ * s), dh = (int)(titleH_ * s);
        SDL_Rect dst{ (vw - dw) / 2, vh / 7, dw, dh };
        SDL_RenderCopy(renderer_, titleTex_, nullptr, &dst);
    } else {
        drawText(vw / 2 - 28, vh / 6, "FFSmith", 20, 255, 255, 255);
    }
    static const char* ITEMS[3] = { "New Game", "Continue", "Debug Menu" };
    int by = vh * 5 / 8;
    for (int i = 0; i < 3; ++i) {
        bool sel = (i == titleSel_), disabled = (i == 1 && !hasSave_);
        int ty = by + i * (fch_ + 7);
        int tx = vw / 2 - 5 * fcw_;
        if (sel && ((blink_ / 16) & 1) == 0) drawText(tx - fcw_ * 2, ty, ">", 2, 255, 240, 120);
        int r = disabled ? 110 : (sel ? 255 : 215), g = disabled ? 110 : 235, b = disabled ? 120 : (sel ? 150 : 255);
        drawText(tx, ty, ITEMS[i], 20, (uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
}

bool Host::charInAnyParty(int charIdx) {
    commitActiveParty();
    for (int s = 0; s < 2; ++s) for (const auto& gm : parties_[s]) if (gm.charIdx == charIdx) return true;
    return false;
}

void Host::renderMenu(int vw, int vh) {
    if (menuPage_ != 0) {
        int px = 6, py = 6, pw = vw - 12, ph = vh - 12;
        drawWindow(px, py, pw, ph, 235);     // FFD window skin (full-page menu)
        if (menuPage_ == 1) renderItemPage(px, py, pw, ph);
        else if (menuPage_ == 4) renderConfigPage(px, py, pw, ph);
        else if (menuPage_ == 7) renderFormationPage(px, py, pw, ph);
        else if (menuPage_ == 5 || menuPage_ == 6) {       // Magic / Job stubs
            drawText(px + 8, py + 4, menuPage_ == 5 ? "MAGIC" : "JOB", 40, 255, 235, 120);
            drawText(px + 10, py + ph / 2 - fch_, "(not yet implemented)", 40, 210, 215, 235);
            drawText(px + 10, py + ph / 2 + 2, "coming soon", 40, 150, 160, 190);
        }
        else                renderCharPage(px, py, pw, ph, menuPage_ == 3);
        drawText(px + pw - 7 * fcw_ - 4, py + ph - fch_ - 2, "X:back", 8, 150, 160, 200);
        return;
    }
    // Root: FFD-style command list (right) + active-party panel (left).
    const int pw = 92, ph = kMenuN * 13 + 12, px = vw - pw - 6, py = 6;
    drawWindow(px, py, pw, ph, 215);         // FFD window skin (command list)
    for (int i = 0; i < kMenuN; ++i) {
        int ty = py + 6 + i * 13;
        if (i == menuCursor_) drawText(px + 5, ty, ">", 2, 255, 240, 120);
        drawText(px + 14, ty, kMenuItems[i], 12, 255, 255, 255);
    }
    // party panel
    const int ppx = 6, ppy = 6, ppw = vw - pw - 18, pph = vh - 12;
    if (ppw > 40) {
        drawWindow(ppx, ppy, ppw, pph, 215);
        drawText(ppx + 6, ppy + 4, partySide_ == 0 ? "Warriors of Light" : "Warriors of Darkness",
                 40, 255, 235, 140);
        const int maxC = fcw_ > 0 ? (ppw - 12) / fcw_ : 20;
        int ry = ppy + 4 + fch_ + 3;
        const int gw = ppw - 16;
        for (int i = 0; i < (int)gameParty_.size(); ++i) {
            const GameMember& gm = gameParty_[i];
            std::string nm = (gm.charIdx >= 0 && gm.charIdx < (int)chars_.size()) ? chars_[gm.charIdx].name : "?";
            int lvl = gm.level > 0 ? gm.level : 1;
            int mhp = memberMaxHp(i), mmp = memberMaxMp(i);
            drawText(ppx + 8, ry, nm + "  L" + std::to_string(lvl), maxC, 235, 240, 255);
            drawText(ppx + 8, ry + fch_ + 1, "HP " + std::to_string(gm.hp) + "/" + std::to_string(mhp), maxC, 200, 220, 245);
            drawGauge(ppx + 8, ry + (fch_ + 1) * 2, gw, 3, gm.hp, mhp, false);
            drawText(ppx + 8, ry + (fch_ + 1) * 2 + 4, "MP " + std::to_string(gm.mp) + "/" + std::to_string(mmp), maxC, 200, 220, 245);
            drawGauge(ppx + 8, ry + (fch_ + 1) * 3 + 4, gw, 3, gm.mp, mmp, true);
            ry += (fch_ + 1) * 3 + 11;
        }
        if (gameParty_.empty()) drawText(ppx + 10, ppy + pph / 2, "(no party)", 40, 205, 205, 215);
    }
}

void Host::renderFormationPage(int px, int py, int pw, int ph) {
    drawText(px + 8, py + 4, "FORMATION", 40, 255, 235, 120);
    drawText(px + pw - 18 * fcw_, py + 4, partySide_ == 0 ? "[Light]" : "[Dark]", 20, 235, 235, 150);
    if (formMode_ == 1) {                                  // add-member picker
        std::vector<int> pool;
        for (int ci = 0; ci < (int)chars_.size(); ++ci) if (!charInAnyParty(ci)) pool.push_back(ci);
        drawText(px + 8, py + 4 + fch_ + 2, "Add member:", 40, 210, 218, 240);
        int top = py + 4 + (fch_ + 2) * 2;
        for (int i = 0; i < (int)pool.size() && top + (i + 1) * (fch_ + 2) < py + ph - fch_ * 2; ++i) {
            int ty = top + i * (fch_ + 2);
            bool sel = (i == formPick_);
            if (sel) drawText(px + 6, ty, ">", 2, 255, 240, 120);
            drawText(px + 16, ty, chars_[pool[i]].name, 30, sel ? 255 : 220, 235, sel ? 150 : 255);
        }
        if (pool.empty()) drawText(px + 16, top, "(roster empty)", 30, 205, 205, 215);
        drawText(px + 8, py + ph - fch_ - 3, "A:add  X:cancel", 40, 160, 185, 150);
        return;
    }
    int top = py + 4 + fch_ + 3;
    for (int i = 0; i < kPartyMax; ++i) {
        int ty = top + i * (fch_ + 4);
        bool sel = (i == formCursor_);
        bool held = (i == formHeld_);
        if (sel) drawText(px + 6, ty, held ? "#" : ">", 2, 255, 240, 120);
        std::string label;
        if (i < (int)gameParty_.size()) {
            const GameMember& gm = gameParty_[i];
            std::string nm = (gm.charIdx >= 0 && gm.charIdx < (int)chars_.size()) ? chars_[gm.charIdx].name : "?";
            label = nm + "   L" + std::to_string(gm.level > 0 ? gm.level : 1)
                  + "  HP " + std::to_string(gm.hp) + "/" + std::to_string(memberMaxHp(i));
        } else label = "---";
        drawText(px + 16, ty, label, 36, held ? 255 : (sel ? 255 : 215), held ? 220 : 235, held ? 120 : (sel ? 150 : 255));
    }
    drawText(px + 8, py + ph - fch_ - 3, "A:hold/swap/add  R:remove  </>:side  X:back", 64, 160, 185, 150);
}

// Resize the window to a chosen aspect ratio.  The renderer derives its logical
// viewport from the window size each frame (vw = winW/scale), so the menus, HUD and
// field viewport all reflow to fill the new shape -- no letterbox.  0,0 keeps the
// window free-form (the default).  We pick a clean logical size for the aspect
// (short side ~180 px) and size the OS window to logical x scale.
void Host::setAspect(int w, int h) {
    cfg_.aspectW = w; cfg_.aspectH = h;
    if (w <= 0 || h <= 0) {                       // free-form: leave the current size
        std::printf("[FFSmith] display: free-form window\n");
        return;
    }
    const int S = 180;                            // short-side logical pixels
    int lw, lh;
    if (w >= h) { lh = S; lw = (S * w + h / 2) / h; }   // landscape -> height is the short side
    else        { lw = S; lh = (S * h + w / 2) / w; }   // portrait  -> width is the short side
    cfg_.logical_width = lw; cfg_.logical_height = lh;
    const int sc = cfg_.scale < 1 ? 1 : cfg_.scale;
    if (window_) SDL_SetWindowSize(window_, lw * sc, lh * sc);
    std::printf("[FFSmith] display: aspect %d:%d -> logical %dx%d (window %dx%d)\n",
                w, h, lw, lh, lw * sc, lh * sc);
}

void Host::renderConfigPage(int px, int py, int pw, int ph) {
    drawText(px + 8, py + 4, "CONFIG", 40, 255, 235, 120);
    const char* aspName = "Free-form";
    for (int i = 0; i < kAspectN; ++i) if (kAspects[i].w == cfg_.aspectW && kAspects[i].h == cfg_.aspectH) aspName = kAspects[i].name;
    static const char* spd[3] = { "Instant", "Normal", "Slow" };
    std::string rows[5] = {
        std::string("Aspect:        < ") + aspName + " >",
        std::string("Window opacity:< ") + std::to_string(winOpacity_ * 100 / 255) + "% >",
        std::string("Window colour: < ") + kWinColors[winColorIdx_ % kWinColorN].name + " >",
        std::string("Text shadow:   < ") + (textShadow_ ? "On" : "Off") + " >",
        std::string("Message speed: < ") + spd[msgSpeed_ % 3] + " >",
    };
    int top = py + 4 + fch_ + 4;
    for (int i = 0; i < 5; ++i) {
        int ty = top + i * (fch_ + 4);
        bool sel = (i == configCursor_);
        if (sel) drawText(px + 6, ty, ">", 2, 255, 240, 120);
        drawText(px + 16, ty, rows[i], 40, sel ? 255 : 225, 235, sel ? 150 : 255);
    }
    drawText(px + 8, py + ph - fch_ - 3, "</> change   X:back", 40, 160, 185, 150);
}

void Host::renderItemPage(int px, int py, int pw, int ph) {
    drawText(px + 8, py + 4, "ITEMS", 40, 255, 235, 120);
    std::string g = commafy(gil_) + " G";
    drawText(px + pw - (int)g.size() * fcw_ - 6, py + 4, g, 12, 235, 225, 150);
    int n = (int)inventory_.size();
    int maxChars = fcw_ > 0 ? (pw - 20) / fcw_ : 30;
    int rowH = fch_ > 0 ? fch_ : 14;
    int descH = rowH * 3 + 6;
    int listTop = py + 22, listBot = py + ph - descH - 4;
    int visRows = (listBot - listTop) / rowH; if (visRows < 1) visRows = 1;
    if (n == 0) { drawText(px + 14, listTop, "(no items)", maxChars, 205, 205, 215); return; }
    if (pageCursor_ >= n) pageCursor_ = n - 1;
    if (pageCursor_ < pageScroll_) pageScroll_ = pageCursor_;
    if (pageCursor_ >= pageScroll_ + visRows) pageScroll_ = pageCursor_ - visRows + 1;
    if (pageScroll_ < 0) pageScroll_ = 0;
    for (int i = 0; i < visRows && pageScroll_ + i < n; ++i) {
        int idx = pageScroll_ + i; const InvSlot& sl = inventory_[idx];
        int ty = listTop + i * rowH; bool sel = (idx == pageCursor_);
        if (sel) drawText(px + 4, ty, ">", 2, 255, 240, 120);
        std::string nm = items_.count(sl.id) ? items_[sl.id].name : ("#" + std::to_string(sl.id));
        std::string cnt = "x" + std::to_string(sl.count);
        drawText(px + 14, ty, nm, maxChars - 4, sel ? 255 : 215, 235, sel ? 170 : 255);
        drawText(px + pw - (int)cnt.size() * fcw_ - 8, ty, cnt, 6, 210, 220, 240);
    }
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 175);
    SDL_Rect db{ px + 3, listBot, pw - 6, descH }; SDL_RenderFillRect(renderer_, &db);
    int sid = inventory_[pageCursor_].id;
    if (!menuMsg_.empty()) drawText(px + 8, listBot + 3, menuMsg_, maxChars - 1, 255, 235, 150);
    else if (items_.count(sid)) drawText(px + 8, listBot + 3, items_[sid].desc, maxChars - 1, 200, 230, 255);
    drawText(px + 8, listBot + 3 + rowH, "[A] Use   [B] Back", 18, 170, 200, 160);
    std::string pos = std::to_string(pageCursor_ + 1) + "/" + std::to_string(n);
    drawText(px + pw - (int)pos.size() * fcw_ - 6, py + 4 + rowH, pos, 12, 180, 190, 220);
}

void Host::renderCharPage(int px, int py, int pw, int ph, bool status) {
    (void)ph;
    drawText(px + 8, py + 4, status ? "STATUS" : "EQUIP", 40, 255, 235, 120);
    if (gameParty_.empty()) { drawText(px + 8, py + 24, "(no party)", 40, 220, 220, 220); return; }
    int pc = pageChar_ % (int)gameParty_.size();
    const GameMember& gm = gameParty_[pc];
    if (gm.charIdx < 0 || gm.charIdx >= (int)chars_.size()) return;
    const CharRec& c = chars_[gm.charIdx];
    int maxChars = fcw_ > 0 ? (pw - 20) / fcw_ : 30;
    std::string hdr = "<  " + c.name + "  >   (" + std::to_string(pc + 1) + "/" + std::to_string(gameParty_.size()) + ")";
    drawText(px + 8, py + 22, hdr, maxChars, 255, 255, 160);
    if (status) {
        int mhp = memberMaxHp(pc), mmp = memberMaxMp(pc);
        drawText(px + 10, py + 42, "Lv " + std::to_string(gm.level > 0 ? gm.level : c.level), maxChars, 235, 235, 255);
        drawText(px + 10, py + 42 + fch_ + 1, "HP " + std::to_string(gm.hp) + "/" + std::to_string(mhp), maxChars, 220, 235, 255);
        drawGauge(px + 10, py + 42 + fch_ * 2 + 1, pw - 28, 4, gm.hp, mhp, false);
        drawText(px + 10, py + 42 + fch_ * 3 + 2, "MP " + std::to_string(gm.mp) + "/" + std::to_string(mmp), maxChars, 220, 235, 255);
        drawGauge(px + 10, py + 42 + fch_ * 4 + 2, pw - 28, 4, gm.mp, mmp, true);
        // job-derived battle attributes (memberStat) -- matches what battle uses
        drawText(px + 10, py + 42 + fch_ * 5 + 4, "STR " + std::to_string(memberStat(pc, 0))
                 + "   SPD " + std::to_string(memberStat(pc, 1)), maxChars, 230, 235, 255);
        drawText(px + 10, py + 42 + fch_ * 6 + 4, "VIT " + std::to_string(memberStat(pc, 2))
                 + "   INT " + std::to_string(memberStat(pc, 3)) + "   MND " + std::to_string(memberStat(pc, 4)), maxChars, 230, 235, 255);
        int w0 = gm.equip[0];
        std::string wpn = (w0 > 0 && items_.count(w0)) ? items_[w0].name : std::string("-");
        drawText(px + 10, py + 42 + fch_ * 8 + 4, "Weapon: " + wpn, maxChars, 200, 220, 255);
    } else {
        static const char* SLOT[6] = { "Weapon", "Off-hand", "Head", "Body", "Accessory", "-" };  // FFD 5 slots (R/L hand, head, body, accessory)
        int top = py + 40;
        for (int k = 0; k < 6; ++k) {
            int ty = top + k * (fch_ + 2);
            int id = gm.equip[k];
            std::string nm = (id > 0 && items_.count(id)) ? items_[id].name
                           : (id ? ("#" + std::to_string(id)) : std::string("-"));
            bool sel = (equipSub_ == 0 && k == equipSlot_);
            if (sel) drawText(px + 4, ty, ">", 2, 255, 240, 120);
            drawText(px + 12, ty, std::string(SLOT[k]) + ": " + nm, maxChars, sel ? 255 : 225, 230, sel ? 170 : 255);
        }
        int fy = top + 6 * (fch_ + 2) + 2;
        if (equipSub_ == 1) {
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 200);
            SDL_Rect cb{ px + 3, fy, pw - 6, ph - (fy - py) - 4 }; SDL_RenderFillRect(renderer_, &cb);
            drawText(px + 8, fy + 2, std::string("Fit ") + SLOT[equipSlot_] + ":", maxChars, 255, 235, 150);
            int rows = (int)equipCand_.size() + 1;
            for (int r = 0; r < rows; ++r) {
                int ty = fy + 2 + (r + 1) * (fch_ + 1);
                if (ty > py + ph - fch_) break;
                bool sel = (r == equipPick_);
                std::string label;
                if (r == 0) label = "[Remove]";
                else { int id = inventory_[equipCand_[r - 1]].id; const Item& it = items_[id];
                       label = it.name + (it.atk ? "  A+" + std::to_string(it.atk) : std::string())
                                       + (it.def ? "  D+" + std::to_string(it.def) : std::string()); }
                if (sel) drawText(px + 8, ty, ">", 2, 255, 240, 120);
                drawText(px + 18, ty, label, maxChars - 2, sel ? 255 : 215, 235, sel ? 170 : 255);
            }
        } else {
            if (!menuMsg_.empty()) drawText(px + 8, fy, menuMsg_, maxChars, 180, 230, 180);
            drawText(px + 8, fy + fch_ + 1, "U/D slot  L/R member  A:change", maxChars, 160, 185, 150);
        }
    }
}

bool Host::loadMenuData(const std::string& dir) {
    auto items = load_items(dir + "/data/items.bin");
    items_.clear(); itemIds_.clear();
    for (auto& it : items) { itemIds_.push_back(it.id); items_[it.id] = std::move(it); }
    chars_ = load_chars(dir + "/data/chars.bin");
    monsters_ = load_monsters(dir + "/data/monsters.bin");
    spells_ = load_spells(dir + "/data/spells.bin");
    jobs_   = load_jobs(dir + "/data/jobs.bin");
    levels_ = load_levels(dir + "/data/levels.bin");
    spriteGeo_ = load_spritegeo(dir + "/data/spritegeo.bin");
    encounters_ = load_encounters(dir + "/data/encounters.bin");
    { size_t nf = 0; for (auto& kv : encounters_) nf += kv.second.size();
      std::printf("[FFSmith] encounters: %zu banks / %zu formations\n", encounters_.size(), nf); }
    if (!audio_.ok()) { audio_.init(dir); audio_.setBgmVolume(0.65f); audio_.setSeVolume(0.85f); }
    Texture bg = load_tex(dir + "/ui/btlbg.tex");
    if (bg.valid() && renderer_) {
        if (btlbgTex_) SDL_DestroyTexture(btlbgTex_);
        btlbgTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, bg.w, bg.h);
        if (btlbgTex_) { SDL_UpdateTexture(btlbgTex_, nullptr, bg.rgba.data(), bg.w * 4); SDL_SetTextureBlendMode(btlbgTex_, SDL_BLENDMODE_BLEND); }
    }
    if (gameParty_.empty() && !chars_.empty()) newGame();
    loadChipAnim(dir);
    std::printf("[FFSmith] menu data: %zu items, %zu chars, %zu monsters; party %zu\n", items_.size(), chars_.size(), monsters_.size(), gameParty_.size());
    return !items_.empty() || !chars_.empty();
}

void Host::setDebugData(std::vector<std::string> maps, std::vector<int> sprites) {
    dbgMaps_ = std::move(maps);
    dbgSprites_ = std::move(sprites);
    if (dbgMapIdx_ >= (int)dbgMaps_.size()) dbgMapIdx_ = 0;
    if (dbgSprIdx_ >= (int)dbgSprites_.size()) dbgSprIdx_ = 0;
}

void Host::debugSelectMap(const std::string& key) {
    for (size_t i = 0; i < dbgMaps_.size(); ++i)
        if (dbgMaps_[i] == key) { dbgMapIdx_ = (int)i; break; }
}

bool Host::consumeDebugStart(DebugStart& out) {
    if (!dbgStart_) return false;
    dbgStart_ = false;
    clearFade();                                  // launcher jumps never start black
    out.map    = (dbgMapIdx_ >= 0 && dbgMapIdx_ < (int)dbgMaps_.size()) ? dbgMaps_[dbgMapIdx_] : std::string();
    out.img    = (dbgSprIdx_ >= 0 && dbgSprIdx_ < (int)dbgSprites_.size()) ? dbgSprites_[dbgSprIdx_] : -1;
    out.x = dbgX_; out.y = dbgY_; out.facing = dbgFacing_; out.noclip = dbgNoclip_;
    overlayOn_ = dbgOverlay_; hudOn_ = dbgHud_;     // apply the view toggles on launch
    return true;
}

void Host::updateDebug(const InputState& in) {
    if (dbgMapIdx_ < 0) dbgMapIdx_ = 0;
    if (dbgSprIdx_ < 0) dbgSprIdx_ = 0;
    const int N = 16;
    if (in.pressed & BTN_UP)   dbgRow_ = (dbgRow_ + N - 1) % N;
    if (in.pressed & BTN_DOWN) dbgRow_ = (dbgRow_ + 1) % N;
    int dir = (in.pressed & BTN_RIGHT) ? 1 : (in.pressed & BTN_LEFT) ? -1 : 0;
    int big = (in.pressed & BTN_R) ? 10 : (in.pressed & BTN_L) ? -10 : 0;
    bool conf = (in.pressed & BTN_CONFIRM) != 0;
    auto cyc = [](int v, int d, int n) { return n <= 0 ? 0 : (((v + d) % n) + n) % n; };
    switch (dbgRow_) {
        case 0: if (dir || big) dbgMapIdx_ = cyc(dbgMapIdx_, dir ? dir : big, (int)dbgMaps_.size()); break;
        case 1: if (dir || big) dbgSprIdx_ = cyc(dbgSprIdx_, dir ? dir : big, (int)dbgSprites_.size()); break;
        case 2: dbgX_ = std::max(0, std::min(255, dbgX_ + dir + big)); break;
        case 3: dbgY_ = std::max(0, std::min(255, dbgY_ + dir + big)); break;
        case 4: if (dir) dbgFacing_ = (((dbgFacing_ + dir) % 4) + 4) % 4; break;
        case 5: if (dir || conf) dbgNoclip_ = !dbgNoclip_; break;
        case 6: if (dir || conf) dbgOverlay_ = !dbgOverlay_; break;
        case 7: if (dir || conf) dbgHud_ = !dbgHud_; break;
        case 8: if (dir) { dbgScale_ = std::max(1, std::min(8, dbgScale_ + dir)); cfg_.scale = dbgScale_; } break;
        case 9: if (dir || conf) {                                  // Aspect ratio (live reflow)
                    dbgAspect_ = cyc(dbgAspect_, dir ? dir : 1, kAspectN);
                    setAspect(kAspects[dbgAspect_].w, kAspects[dbgAspect_].h);
                } break;
        case 10: if (dir || conf) { encountersOn_ = !encountersOn_; } break;  // random encounters
        case 11: if (dir || conf) {                                 // desktop fullscreen
                    dbgFullscreen_ = !dbgFullscreen_;
                    if (window_) SDL_SetWindowFullscreen(window_, dbgFullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                } break;
        case 12: if (dir) winOpacity_ = std::max(80, std::min(255, winOpacity_ + dir * 10)); break;  // window opacity
        case 13: if (dir || conf) winColorIdx_ = (winColorIdx_ + (dir ? dir : 1) + kWinColorN) % kWinColorN; break;  // window colour
        case 14: if (dir || conf) {                                 // active party side (Formation = full builder)
                    if (gameParty_.empty() && parties_[0].empty() && parties_[1].empty()) newGame();
                    switchSide();
                } break;
        default: break;
    }
    if (conf && dbgRow_ == 15) dbgStart_ = true;
}

void Host::renderDebug() {
    int winW = 0, winH = 0; SDL_GetWindowSize(window_, &winW, &winH);
    const int sc = cfg_.scale < 1 ? 1 : cfg_.scale;
    int vw = winW / sc; if (vw < 16) vw = 16;
    int vh = winH / sc; if (vh < 16) vh = 16;
    SDL_RenderSetScale(renderer_, (float)sc, (float)sc);
    SDL_SetRenderDrawColor(renderer_, 10, 12, 30, 255);
    SDL_RenderClear(renderer_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    static const char* fac[4] = {"Down", "Up", "Left", "Right"};
    if (dbgMapIdx_ < 0) dbgMapIdx_ = 0;
    if (dbgSprIdx_ < 0) dbgSprIdx_ = 0;
    std::string mapName = (dbgMapIdx_ >= 0 && dbgMapIdx_ < (int)dbgMaps_.size()) ? dbgMaps_[dbgMapIdx_] : std::string("(no maps)");
    int sprId = (dbgSprIdx_ >= 0 && dbgSprIdx_ < (int)dbgSprites_.size()) ? dbgSprites_[dbgSprIdx_] : -1;
    std::string rows[16] = {
        "Map:       < " + mapName + " >   (" + std::to_string(dbgMapIdx_ + 1) + "/" + std::to_string(dbgMaps_.size()) + ")",
        "Character: < fldchr" + std::to_string(sprId) + " >",
        "Spawn X:   < " + std::to_string(dbgX_) + " >",
        "Spawn Y:   < " + std::to_string(dbgY_) + " >",
        std::string("Facing:    < ") + fac[dbgFacing_ & 3] + " >",
        std::string("No-clip:   < ") + (dbgNoclip_ ? "On" : "Off") + " >",
        std::string("Collision: < ") + (dbgOverlay_ ? "On" : "Off") + " >",
        std::string("HUD:       < ") + (dbgHud_ ? "On" : "Off") + " >",
        std::string("Scale:     < ") + std::to_string(dbgScale_) + "x >",
        std::string("Aspect:    < ") + kAspects[dbgAspect_ % kAspectN].name + " >",
        std::string("Encounters:< ") + (encountersOn_ ? "On" : "Off") + " >",
        std::string("Fullscreen:< ") + (dbgFullscreen_ ? "On" : "Off") + " >",
        std::string("Win opacity:< ") + std::to_string(winOpacity_ * 100 / 255) + "% >",
        std::string("Win colour:< ") + kWinColors[winColorIdx_ % kWinColorN].name + " >",
        std::string("Party:     < ") + (partySide_ == 0 ? "Light" : "Dark") + " >  (Formation = full builder)",
        std::string("START"),
    };
    int x = 12, y = 10;
    drawText(x, y, "FFSmith  -  Debug Launcher", 60, 255, 235, 120);
    y += 22;
    for (int i = 0; i < 16; ++i) {
        int ry = y + i * 11 + (i == 15 ? 6 : 0);
        if (i == dbgRow_) drawText(x, ry, ">", 2, 255, 240, 120);
        bool start = (i == 15);
        drawText(x + 12, ry, rows[i], 64, start ? 255 : 230, 235, start ? 120 : 255);
    }
    drawText(x, vh - 13, "arrows move/change  .  L/R jump 10  .  Z toggle/START  .  (F1 menu, F2 noclip)",
             90, 150, 160, 200);
}

int Host::firstLiving(int from) const {
    for (int i = std::max(0, from); i < (int)party_.size(); ++i) if (party_[i].hp > 0) return i;
    return -1;
}
bool Host::partyAlive() const { for (const auto& c : party_) if (c.hp > 0) return true; return false; }
int Host::firstLivingEnemy(int from) const {
    for (int i = std::max(0, from); i < (int)enemies_.size(); ++i) if (enemies_[i].hp > 0) return i;
    return -1;
}
bool Host::enemiesAlive() const { for (const auto& e : enemies_) if (e.hp > 0) return true; return false; }

void Host::doEnemyAttack() {
    std::vector<int> alive;
    for (int i = 0; i < (int)party_.size(); ++i) if (party_[i].hp > 0) alive.push_back(i);
    if (alive.empty() || enemyActor_ < 0 || enemyActor_ >= (int)enemies_.size()) { btlPhase_ = 4; return; }
    Combatant& e = enemies_[enemyActor_];
    Combatant& v = party_[alive[std::rand() % alive.size()]];
    int d = physDamage(e, v);
    if (v.defending) d = std::max(1, d / 2);
    v.hp = std::max(0, v.hp - d);
    btlMsg_ = e.name + " -> " + v.name + "   " + std::to_string(d) + " dmg";
    btlPhase_ = 2;
}

void Host::buildSpellList() {
    spellList_.clear();
    if (btlMember_ < 0 || btlMember_ >= (int)party_.size()) return;
    const Combatant& m = party_[btlMember_];
    for (int i = 0; i < (int)spells_.size(); ++i) {
        const Spell& sp = spells_[i];
        bool known = (sp.type == 1 && m.mnd >= 2) || (sp.type == 0 && m.intl >= 2);
        if (known && sp.mp <= m.mp) spellList_.push_back(i);
    }
}

void Host::castOn(int targetIsEnemy, int idx) {
    (void)targetIsEnemy;
    if (pendingSpell_ < 0 || pendingSpell_ >= (int)spells_.size()) { btlPhase_ = 1; return; }
    Combatant& caster = party_[btlMember_];
    const Spell& sp = spells_[pendingSpell_];
    caster.mp = std::max(0, caster.mp - sp.mp);
    if (sp.type == 0) {                                      // damage (INT-scaled)
        Combatant& t = enemies_[idx];
        int d = std::max(1, sp.power + caster.intl * 3 - t.def / 4 + (std::rand() % 9 - 4));
        t.hp = std::max(0, t.hp - d);
        btlMsg_ = caster.name + " casts " + sp.name + "!  " + std::to_string(d) + " dmg";
    } else {                                                // heal (MND-scaled)
        Combatant& t = party_[idx];
        int hgain = sp.power + caster.mnd * 2;
        t.hp = std::min(t.maxhp, t.hp + hgain);
        btlMsg_ = caster.name + " casts " + sp.name + " on " + t.name + "!  +" + std::to_string(hgain) + " HP";
    }
    pendingSpell_ = -1;
    btlPhase_ = 1;
}

int Host::physDamage(const Combatant& a, const Combatant& d) const {
    // Decoded core of BattleClass::CalcPhysicAttackDmg via the BTLACT map: an attack term
    // (weaponPower W<<6) + random spread, minus full defense (D<<6), scaled by the
    // (3*attackStat + level) power factor and >>0xc (the engine's ×4 / 4096).
    int W = std::max(1, a.wpn), A = std::max(1, a.atk), L = a.level, D = d.def;
    int attackTerm = W * 64;
    int spread = std::rand() % (std::max(128, W * 64 / 20) + 1);
    int afterDef = std::max(0, attackTerm + spread - D * 64);
    int powerFac = 3 * A + L;
    return std::max(1, afterDef * powerFac / 1024);
}

void Host::pickNextActor() {
    const int THRESH = 256;
    for (int guard = 0; guard < 200000; ++guard) {
        for (auto& c : party_)   if (c.hp > 0) c.atb += std::max(1, c.spd);
        for (auto& e : enemies_) if (e.hp > 0) e.atb += std::max(1, e.spd);
        int best = -1, bestVal = THRESH - 1; bool be = false;
        for (int i = 0; i < (int)party_.size(); ++i)
            if (party_[i].hp > 0 && party_[i].atb > bestVal) { bestVal = party_[i].atb; best = i; be = false; }
        for (int i = 0; i < (int)enemies_.size(); ++i)
            if (enemies_[i].hp > 0 && enemies_[i].atb > bestVal) { bestVal = enemies_[i].atb; best = i; be = true; }
        if (best >= 0) { curIsEnemy_ = be; curIdx_ = best; (be ? enemies_[best].atb : party_[best].atb) -= THRESH; return; }
    }
    curIsEnemy_ = false; curIdx_ = std::max(0, firstLiving(0));
}

void Host::beginNextTurn() {
    pickNextActor();
    if (curIsEnemy_) { enemyActor_ = curIdx_; doEnemyAttack(); }    // -> phase 2 + message
    else {
        btlMember_ = curIdx_;
        if (btlMember_ >= 0 && btlMember_ < (int)party_.size()) party_[btlMember_].defending = false;
        btlCmd_ = 0; btlPhase_ = 0;
    }
}

// Per-job growth percent for the member's current job (GameClass::SetJobStatus):
// maxHP = base_hp[level] * jobHpPct / 100, likewise MP.  Defaults to 100% when
// the job isn't in the table.
int Host::memberJobPct(int i, bool mp) const {
    if (i < 0 || i >= (int)gameParty_.size()) return 100;
    int ci = gameParty_[i].charIdx;
    int job = (ci >= 0 && ci < (int)chars_.size()) ? chars_[ci].job : 0;
    auto it = jobs_.find(job);
    if (it == jobs_.end()) return 100;
    return mp ? it->second.mpPct : it->second.hpPct;
}
int Host::memberMaxHp(int i) const {
    if (i < 0 || i >= (int)gameParty_.size()) return 1;
    int ci = gameParty_[i].charIdx;
    int lvl = gameParty_[i].level > 0 ? gameParty_[i].level : (ci >= 0 && ci < (int)chars_.size() ? chars_[ci].level : 1);
    if (levels_.valid()) return std::max(1, levels_.maxHp(lvl) * memberJobPct(i, false) / 100);
    return (ci >= 0 && ci < (int)chars_.size() && chars_[ci].hp > 0) ? chars_[ci].hp : 30;
}
int Host::memberMaxMp(int i) const {
    if (i < 0 || i >= (int)gameParty_.size()) return 0;
    int ci = gameParty_[i].charIdx;
    int lvl = gameParty_[i].level > 0 ? gameParty_[i].level : (ci >= 0 && ci < (int)chars_.size() ? chars_[ci].level : 1);
    if (levels_.valid()) return std::max(0, levels_.maxMp(lvl) * memberJobPct(i, true) / 100);
    return (ci >= 0 && ci < (int)chars_.size()) ? chars_[ci].mp : 0;
}

// Job-derived battle attribute (GameClass::SetJobStatus, libjniproxy 152644-152705):
// each of the five attributes = max(1, base_stat[level] * jobStatPct / 100), where
// base_stat is the shared per-level boot-section-8 byte (FLVL base[]) and jobStatPct
// is the member's job percent for that attribute (FJOB body[11..15]).  Equipment /
// learned-ability stat bonuses are not yet decoded, so they are omitted here (the
// weapon W and armor D still come from equipped gear).  which: 0=STR 1=SPD 2=VIT
// 3=INT 4=MND.  Falls back to the raw chara_set attribute when no level table is
// loaded (keeps the engine playable on an older bundle).
int Host::memberStat(int i, int which) const {
    if (i < 0 || i >= (int)gameParty_.size()) return 1;
    int ci = gameParty_[i].charIdx;
    const CharRec* c = (ci >= 0 && ci < (int)chars_.size()) ? &chars_[ci] : nullptr;
    int lvl = gameParty_[i].level > 0 ? gameParty_[i].level : (c ? c->level : 1);
    int pct = 100;
    if (c) { auto it = jobs_.find(c->job);
        if (it != jobs_.end()) { const JobGrowth& g = it->second;
            pct = which==0?g.str : which==1?g.spd : which==2?g.vit : which==3?g.intl : g.mnd; } }
    if (levels_.valid()) return std::max(1, levels_.baseStat(lvl) * pct / 100);
    if (!c) return 1;
    return std::max(1, which==0?c->str : which==1?c->spd : which==2?c->vit : which==3?c->intl : c->mnd);
}

GameMember Host::makeMember(int charIdx) const {
    GameMember m; m.charIdx = charIdx;
    if (charIdx >= 0 && charIdx < (int)chars_.size()) {
        const CharRec& c = chars_[charIdx];
        m.hp = c.hp > 0 ? c.hp : 30; m.mp = c.mp;
        for (int k = 0; k < 6; ++k) m.equip[k] = c.equip[k];
        m.level = c.level > 0 ? c.level : 1;
    } else { m.hp = 30; m.level = 1; }
    m.exp = levels_.valid() ? (int)levels_.expForLevel(m.level) : 0;
    return m;
}

void Host::newGame() {
    clearFade();
    // FFD: two parties of up to kPartyMax.  Warriors of Light take the first roster
    // slots (Sol & co), Warriors of Darkness the next.  Both are debug/Formation
    // editable; the active side starts on Light.
    parties_[0].clear(); parties_[1].clear();
    int n = (int)chars_.size();
    for (int i = 0; i < kPartyMax && i < n; ++i) parties_[0].push_back(makeMember(i));
    for (int i = 0; i < kPartyMax && (kPartyMax + i) < n; ++i) parties_[1].push_back(makeMember(kPartyMax + i));
    partySide_ = 0;
    gameParty_ = parties_[0];
    inventory_ = { {420, 5}, {426, 2}, {21, 1}, {22, 1}, {295, 1}, {237, 1}, {215, 1}, {408, 1} };  // Potions + Phoenix Down + spare gear
    gil_ = 500;
}

// Swap the active party between Warriors of Light (0) and Darkness (1).
void Host::switchSide() {
    commitActiveParty();
    partySide_ ^= 1;
    gameParty_ = parties_[partySide_];
    std::printf("[FFSmith] active party -> %s (%zu members)\n",
                partySide_ == 0 ? "Light" : "Dark", gameParty_.size());
}

std::string Host::awardBattleRewards() {
    if (rewardsGiven_) return "Victory!";
    rewardsGiven_ = true;
    long totExp = 0, totGil = 0;
    for (const auto& e : enemies_) { totExp += e.exp; totGil += e.gil; }
    gil_ = (int)std::min<long>(9999999, (long)gil_ + totGil);
    std::string msg = "Win!  +" + commafy(totExp) + " EXP  +" + commafy(totGil) + " G";
    std::string ups;
    for (auto& gm : gameParty_) {
        if (gm.hp <= 0) continue;                                   // KO'd members earn nothing
        gm.exp = (int)std::min<long>(9999999, (long)gm.exp + totExp);
        int newLvl = levels_.valid() ? levels_.levelFromExp(gm.exp) : gm.level;
        if (newLvl > gm.level) {
            int job = (gm.charIdx >= 0 && gm.charIdx < (int)chars_.size()) ? chars_[gm.charIdx].job : 0;
            auto jit = jobs_.find(job);
            int hpPct = jit != jobs_.end() ? jit->second.hpPct : 100;
            int mpPct = jit != jobs_.end() ? jit->second.mpPct : 100;
            int oldHp = levels_.maxHp(gm.level) * hpPct / 100, oldMp = levels_.maxMp(gm.level) * mpPct / 100;
            gm.level = newLvl;
            gm.hp += std::max(0, levels_.maxHp(newLvl) * hpPct / 100 - oldHp);   // gain the HP/MP delta (job-scaled)
            gm.mp += std::max(0, levels_.maxMp(newLvl) * mpPct / 100 - oldMp);
            if (gm.charIdx >= 0 && gm.charIdx < (int)chars_.size())
                ups += (ups.empty() ? "" : ", ") + chars_[gm.charIdx].name + " L" + std::to_string(newLvl);
        }
    }
    if (!ups.empty()) msg += "   LEVEL UP: " + ups;
    std::printf("[FFSmith] %s\n", msg.c_str());
    return msg;
}

void Host::endBattle() {                                    // persist battle HP/MP back to the party
    for (size_t i = 0; i < party_.size() && i < gameParty_.size(); ++i) {
        gameParty_[i].hp = std::max(0, party_[i].hp);
        gameParty_[i].mp = std::max(0, party_[i].mp);
    }
    commitActiveParty();                                    // keep the active side's stored copy in sync
    mode_ = Mode::Field;
    if (scriptBattle_) {
        scriptBattle_ = false; battleNoEscape_ = false;
        lastBattleResult_ = battleOutcome_;
        // Scripted-battle defeat must not strand the script (the prologue
        // battle is a scripted loss): revive the party to 1 HP and resume.
        // APPROXIMATION: the original's loss handling per-formation is not
        // yet decoded (battle scripts in bsc.dat may control this).
        if (battleOutcome_ == 0)
            for (auto& gm : gameParty_) if (gm.hp <= 0) gm.hp = 1;
        if (field_) field_->resumeAfterBattle();
    }
    battleOutcome_ = 1;
}

int Host::currentBank() const {
    int g = 0;
    std::sscanf(mapKey_.c_str(), "g%d", &g);
    return g;
}

// 0x50 ScriptEncount hand-off: look the formation up in the current story
// bank's table and fight it; resume the script when the battle ends.
void Host::startFormationBattle(const VMEncounter& enc) {
    const Formation* fm = nullptr;
    auto bi = encounters_.find(currentBank());
    if (bi != encounters_.end()) {
        auto fi = bi->second.find(enc.formation);
        if (fi != bi->second.end()) fm = &fi->second;
    }
    if (!fm || fm->enemies.empty()) {
        std::printf("[FFSmith] formation %d (bank %d) not found -> skipping battle\n",
                    enc.formation, currentBank());
        lastBattleResult_ = 1;
        if (field_) field_->resumeAfterBattle();
        return;
    }
    enemies_.clear(); rewardsGiven_ = false; floats_.clear();
    for (const auto& fe : fm->enemies) {
        const Monster* m = nullptr;
        for (const auto& mm : monsters_) if (mm.id == fe.id) { m = &mm; break; }
        if (!m) continue;
        Combatant e;
        e.name = m->name; e.hp = e.maxhp = m->hp;
        e.wpn = std::max(1, m->atk);      // W = weapon-attack (monster body[15])
        e.atk = std::max(1, m->level);    // A = LEVEL (SetBtlEnemyParam: BTLACT+0x3c)
        e.def = std::max(0, m->def);      // D (body[18])
        e.level = m->level;
        e.spd = std::max(1, m->level);    // BTLACT+0x40 (SPD-class) = level for enemies
        e.mp = e.maxmp = m->hp / 8;       // enemy MP = HP/8 (SetBtlEnemyParam)
        e.exp = m->exp; e.gil = m->gil;
        e.sprite = m->sprite; e.frames = m->frames;
        enemies_.push_back(e);
    }
    if (enemies_.empty()) {
        lastBattleResult_ = 1;
        if (field_) field_->resumeAfterBattle();
        return;
    }
    std::unordered_map<std::string, int> total, idx;          // Goblin A / B
    for (auto& e : enemies_) total[e.name]++;
    for (auto& e : enemies_) {
        std::string base = e.name;
        if (total[base] > 1) { e.name = base + " " + std::string(1, char('A' + idx[base])); idx[base]++; }
    }
    party_.clear();
    if (gameParty_.empty()) newGame();
    for (size_t gi = 0; gi < gameParty_.size(); ++gi) {
        const GameMember& gm = gameParty_[gi];
        if (gm.charIdx < 0 || gm.charIdx >= (int)chars_.size()) continue;
        const CharRec& c = chars_[gm.charIdx];
        int weaponAtk = (gm.equip[0] > 0 && items_.count(gm.equip[0])) ? items_[gm.equip[0]].atk : 0;
        int armorDef = 0;
        for (int k = 0; k < 6; ++k) if (gm.equip[k] > 0 && items_.count(gm.equip[k])) armorDef += items_[gm.equip[k]].def;
        Combatant cb;
        cb.name = c.name;
        cb.maxhp = memberMaxHp((int)gi);
        cb.hp = std::max(0, std::min(gm.hp, cb.maxhp));
        cb.maxmp = memberMaxMp((int)gi); cb.mp = std::max(0, std::min(gm.mp, cb.maxmp));
        cb.atk = std::max(1, c.str);
        cb.wpn = std::max(3, weaponAtk);
        cb.def = std::max(1, c.vit + armorDef + c.level / 2);
        cb.level = gm.level > 0 ? gm.level : c.level; cb.spd = std::max(1, c.spd);
        cb.intl = c.intl; cb.mnd = c.mnd;
        party_.push_back(cb);
    }
    if (party_.empty()) { Combatant cb; cb.name = "Hero"; cb.hp = cb.maxhp = 34; cb.atk = 13; cb.wpn = 6; cb.def = 6; party_.push_back(cb); }
    scriptBattle_ = true;
    battleNoEscape_ = (fm->noEscape != 0);
    battleOutcome_ = 1;
    if (enc.bgm >= 0) audio_.playBgm(enc.bgm);
    else if (field_ && field_->map() && field_->map()->battle_bgm != 255)
        audio_.playBgm(field_->map()->battle_bgm);
    target_ = firstLivingEnemy(0); enemyActor_ = 0;
    for (auto& c : party_)   c.atb = std::rand() % 256;
    for (auto& e : enemies_) e.atb = std::rand() % 256;
    btlCmd_ = 0; btlMsg_.clear();
    mode_ = Mode::Battle;
    beginNextTurn();
    std::printf("[FFSmith] scripted battle: formation %d (bank %d), %zu enemies%s\n",
                enc.formation, currentBank(), enemies_.size(),
                battleNoEscape_ ? " [no escape]" : "");
}

// Random encounters (--encounters). The per-map encounter AREAS and their
// formation ids are decoded data (FFM5); the per-step ROLL is an
// APPROXIMATION (the original's step/ratio formula is not yet decoded):
// chance per new cell = sum(rates of covering areas) / 256, area picked
// weighted by rate.
void Host::maybeRandomEncounter() {
    if (!field_ || mode_ != Mode::Field) return;
    const FfMap* mp = field_->map();
    if (!mp || mp->enc_areas.empty() || field_->moving()) return;
    int cell = field_->row() * mp->w + field_->col();
    if (cell == lastEncCell_) return;
    lastEncCell_ = cell;
    int cx = field_->col(), cy = field_->row();
    int totalRate = 0;
    std::vector<const EncArea*> covering;
    for (const auto& a : mp->enc_areas)
        if (cx >= a.x && cx < a.x + a.w && cy >= a.y && cy < a.y + a.h) {
            covering.push_back(&a); totalRate += std::max(0, a.rate);
        }
    if (covering.empty() || totalRate <= 0) return;
    if (std::rand() % 256 >= totalRate) return;
    int pick = std::rand() % totalRate;
    const EncArea* chosen = covering.back();
    for (const EncArea* a : covering) { pick -= std::max(0, a->rate); if (pick < 0) { chosen = a; break; } }
    VMEncounter enc;
    enc.formation = chosen->set_id;
    std::printf("[FFSmith] random encounter: area set %d @(%d,%d)\n", chosen->set_id, cx, cy);
    startFormationBattle(enc);
    scriptBattle_ = false;            // random battles don't resume a script
}

void Host::selfTestCutscene() {
    if (!field_) { std::printf("[cuttest] no field\n"); return; }
    auto hexblk = [](const char* hx) {
        std::vector<uint8_t> v;
        for (const char* q = hx; q[0] && q[1]; q += 2) {
            auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
            v.push_back((uint8_t)((nib(q[0]) << 4) | nib(q[1])));
        }
        return v;
    };
    // pick an actor with a u8-addressable id
    const Actor* pick = nullptr;
    for (const auto& a : field_->actors()) if (a.id >= 0 && a.id <= 255) { pick = &a; break; }
    if (!pick) { std::printf("[cuttest] no addressable actor\n"); return; }
    int id = pick->id, c0 = pick->col, r0 = pick->row;
    char b0[32];
    std::snprintf(b0, sizeof(b0), "68%02x000004" "0303001c", id);  // walk R,R,D + face player
    static Event ev;
    ev.scripts.clear();
    ev.scripts.push_back(hexblk(b0));
    ev.scripts.push_back(hexblk("69"));                  // wait for the actor
    ev.scripts.push_back(hexblk("32000a"));              // wait 10 ticks
    ev.scripts.push_back(hexblk("0400000000060001"));    // resume proof: set flag0[6]
    ev.scripts.push_back(hexblk("57"));
    field_->debugRunEvent(&ev);
    InputState none{};
    int t = 0;
    while (t++ < 600 && !scriptState_.getFlag(0, 6)) field_->update(none);
    const Actor* after = nullptr;
    for (const auto& a : field_->actors()) if (a.id == id) { after = &a; break; }
    bool moved = after && (after->col == c0 + 2) && (after->row == r0 + 1);
    bool resumed = scriptState_.getFlag(0, 6) != 0;
    std::printf("[cuttest] actor %d (%d,%d)->(%d,%d) facing=%d ticks=%d\n",
                id, c0, r0, after ? after->col : -1, after ? after->row : -1,
                after ? after->facing : -1, t);
    std::printf("[cuttest] %s (moved=%d, wait+resume=%d)\n",
                (moved && resumed) ? "PASS" : "FAIL", (int)moved, (int)resumed);
}

void Host::selfTestEncounter() {
    if (gameParty_.empty()) newGame();
    if (!field_) { std::printf("[enctest] no field\n"); return; }
    auto hexblk = [](const char* hx) {
        std::vector<uint8_t> v;
        for (const char* q = hx; q[0] && q[1]; q += 2) {
            auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
            v.push_back((uint8_t)((nib(q[0]) << 4) | nib(q[1])));
        }
        return v;
    };
    static Event ev;                                  // outlives the battle
    ev.scripts.clear();
    // blk0: 0x50 formation 1 (imm), bg 0/0, cond 0, bgm -1/-1, flags 1
    ev.scripts.push_back(hexblk("50" "000001" "000000" "000000" "000000" "00ffff" "00ffff" "000001"));
    ev.scripts.push_back(hexblk("0400000000050001"));  // blk1 (resume): set flag0[5]
    ev.scripts.push_back(hexblk("57"));
    field_->debugRunEvent(&ev);
    std::printf("[enctest] pending=%d\n", (int)field_->encounterPending());
    if (!field_->encounterPending()) { std::printf("[enctest] FAIL: no pending encounter\n"); return; }
    startFormationBattle(field_->startEncounter());
    if (mode_ != Mode::Battle) { std::printf("[enctest] battle skipped (formation missing?)\n"); }
    else {
        InputState c; c.pressed = BTN_CONFIRM;
        int guard = 0;
        while (mode_ == Mode::Battle && guard++ < 800) updateBattle(c);
        std::printf("[enctest] battle done in %d steps, outcome=%d\n", guard, lastBattleResult_);
    }
    bool resumed = scriptState_.getFlag(0, 5) != 0;
    long ref = vmEnv_.battleRef ? vmEnv_.battleRef(3, 0) : -1;
    std::printf("[enctest] %s (resume flag0[5]=%d, battleRef t3=%ld)\n",
                resumed ? "PASS" : "FAIL", (int)resumed, ref);
}

void Host::selfTestLevel() {
    if (gameParty_.empty()) newGame();
    GameMember& gm = gameParty_[0];
    std::printf("[leveltest] %s  L%d  exp%d  HP%d/%d  gil%d\n",
                chars_[gm.charIdx].name.c_str(), gm.level, gm.exp, gm.hp, memberMaxHp(0), gil_);
    enemies_.clear(); rewardsGiven_ = false; floats_.clear();       // synthetic enemy worth a couple levels
    Combatant e; e.exp = (long)levels_.expForLevel(gm.level + 2) - gm.exp + 5; e.gil = 120; enemies_.push_back(e);
    awardBattleRewards();
    std::printf("[leveltest] -> %s  L%d  exp%d  HP%d/%d  gil%d\n",
                chars_[gm.charIdx].name.c_str(), gm.level, gm.exp, gm.hp, memberMaxHp(0), gil_);
}

// N2 job-stat derivation check (GameClass::SetJobStatus).  For each party member
// it independently recomputes attr = max(1, base_stat[level] * jobStatPct / 100)
// from the loaded level + job tables and asserts memberStat() agrees, then prints
// the derived combatant attributes for eyeball verification.  PASS requires the
// FLVL base-stat trailer to be present and every attribute to match + be in range.
void Host::selfTestJobStat() {
    if (gameParty_.empty()) newGame();
    bool ok = true;
    if (!levels_.valid() || levels_.base.empty()) {
        std::printf("[jobstattest] WARN: bundle has no FLVL base-stat trailer (re-bake with toolkit >=0.7.28)\n");
        ok = false;
    }
    static const char* SN[5] = {"STR","SPD","VIT","INT","MND"};
    for (size_t i = 0; i < gameParty_.size(); ++i) {
        int ci = gameParty_[i].charIdx;
        if (ci < 0 || ci >= (int)chars_.size()) continue;
        const CharRec& c = chars_[ci];
        int lvl = gameParty_[i].level > 0 ? gameParty_[i].level : c.level;
        int bstat = levels_.valid() ? levels_.baseStat(lvl) : 0;
        JobGrowth g; auto it = jobs_.find(c.job); if (it != jobs_.end()) g = it->second;
        int pct[5] = { g.str, g.spd, g.vit, g.intl, g.mnd };
        std::printf("[jobstattest] %-8s job%d L%d  baseHP%d baseMP%d baseStat%d  HP%d MP%d |",
                    c.name.c_str(), c.job, lvl, levels_.maxHp(lvl), levels_.maxMp(lvl), bstat,
                    memberMaxHp((int)i), memberMaxMp((int)i));
        for (int w = 0; w < 5; ++w) {
            int got = memberStat((int)i, w);
            int exp = levels_.valid() ? std::max(1, bstat * pct[w] / 100)
                                      : std::max(1, w==0?c.str:w==1?c.spd:w==2?c.vit:w==3?c.intl:c.mnd);
            bool good = (got == exp) && got >= 1 && got <= 999;
            if (!good) ok = false;
            std::printf("  %s%d(%d%%)%s", SN[w], got, pct[w], good ? "" : "!=EXP");
        }
        std::printf("\n");
    }
    std::printf("[jobstattest] %s\n", ok ? "PASS" : "FAIL");
}

void Host::selfTestMenu() {
    if (startMap_.empty()) setStartMap("g0_p0_m500");
    hasSave_ = true;
    mode_ = Mode::Title; titleSel_ = 0; input_ = InputState{}; input_.pressed = BTN_CONFIRM; update(0.0);   // -> Intro
    bool inIntro = (mode_ == Mode::Intro);
    for (int k = 0; k < 3; ++k) { input_ = InputState{}; input_.pressed = BTN_CONFIRM; update(0.0); }          // advance the 3 intro beats
    DebugStart ds; bool got = consumeDebugStart(ds);
    std::printf("[menutest] New Game -> Intro=%d, after 3 beats start map=%s, party reset to %zu members\n", (int)inIntro, got ? ds.map.c_str() : "<none>", gameParty_.size());
    mode_ = Mode::Title; titleSel_ = 1; loadReq_ = false; input_ = InputState{}; input_.pressed = BTN_CONFIRM; update(0.0);
    std::printf("[menutest] Continue -> loadReq=%d (hasSave=%d)\n", (int)consumeLoadRequest(), (int)hasSave_);
    mode_ = Mode::Title; titleSel_ = 2; input_ = InputState{}; input_.pressed = BTN_CONFIRM; update(0.0);
    std::printf("[menutest] Debug Menu -> mode is Debug? %d\n", (int)(mode_ == Mode::Debug));
}

void Host::selfTestRevive() {
    if (gameParty_.empty()) newGame();
    GameMember& gm = gameParty_[1];
    gm.hp = 0;
    std::printf("[revivetest] %s KO'd (hp 0)\n", chars_[gm.charIdx].name.c_str());
    int idx = -1; for (int i = 0; i < (int)inventory_.size(); ++i) if (inventory_[i].id == 426) { idx = i; break; }
    if (idx < 0) { std::printf("[revivetest] no Phoenix Down in bag\n"); return; }
    useItem(idx);
    std::printf("[revivetest] %s HP now %d/%d   msg=\"%s\"\n",
                chars_[gm.charIdx].name.c_str(), gm.hp, memberMaxHp(1), menuMsg_.c_str());
}

void Host::selfTestEquip() {
    if (gameParty_.empty()) newGame();
    pageChar_ = 0;                                   // Sol
    GameMember& gm = gameParty_[0];
    static const char* SLOT[6] = { "Weapon", "Off-hand", "Head", "Body", "Accessory", "-" };  // FFD 5 slots (R/L hand, head, body, accessory)
    for (int sl = 0; sl < 6; ++sl) {
        buildEquipCandidates(sl);
        std::printf("[equiptest] slot%d %-8s fits:", sl, SLOT[sl]);
        if (equipCand_.empty()) std::printf(" (none)");
        for (int ci : equipCand_) { int id = inventory_[ci].id; std::printf(" %s(t%d)", items_[id].name.c_str(), items_[id].type); }
        std::printf("\n");
    }
    auto atkOf = [&](int id) { return (id > 0 && items_.count(id)) ? items_[id].atk : 0; };
    buildEquipCandidates(0);
    if (!equipCand_.empty()) {
        int wasId = gm.equip[0];
        swapEquip(0, 1);
        std::printf("[equiptest] slot0 swap: %s(A%d) -> %s(A%d)   msg=\"%s\"\n",
                    items_.count(wasId) ? items_[wasId].name.c_str() : "-", atkOf(wasId),
                    items_[gm.equip[0]].name.c_str(), atkOf(gm.equip[0]), menuMsg_.c_str());
    }
}

void Host::selfTestItemUse() {
    if (gameParty_.empty()) newGame();
    if (inventory_.empty()) { std::printf("[itemtest] inventory empty\n"); return; }
    int m = 0, before = inventory_[0].count;
    gameParty_[m].hp = 1;
    std::printf("[itemtest] %s HP %d/%d, holding %s x%d\n",
                chars_[gameParty_[m].charIdx].name.c_str(), gameParty_[m].hp, memberMaxHp(m),
                items_.count(inventory_[0].id) ? items_[inventory_[0].id].name.c_str() : "?", before);
    useItem(0);
    int after = inventory_.empty() ? 0 : inventory_[0].count;
    std::printf("[itemtest] -> %s HP now %d/%d, Potion x%d   msg=\"%s\"\n",
                chars_[gameParty_[m].charIdx].name.c_str(), gameParty_[m].hp, memberMaxHp(m), after, menuMsg_.c_str());
}

int Host::curMember() const { return gameParty_.empty() ? 0 : (pageChar_ % (int)gameParty_.size()); }

bool Host::slotAcceptsItem(int slot, const Item& it) const {
    // item_type category (baked 0.7.13+): 1-15 weapon, 16 shield, 17-19 head,
    // 20-22 body, 23 hands/accessory; 0 = consumable/key (never equippable).
    int t = it.type;
    bool weapon = (t >= 1 && t <= 15), shield = (t == 16);
    bool head = (t >= 17 && t <= 19), body = (t >= 20 && t <= 22), handAcc = (t == 23);
    switch (slot) {
        case 0: return weapon;                 // Weapon
        case 1: return weapon || shield;       // Off-hand: weapon (dual-wield) or shield
        case 2: return head;                   // Head
        case 3: return body;                   // Body
        case 4: return handAcc;                // Arms
        case 5: return handAcc;                // Accessory (shares category 23)
        default: return false;
    }
}

void Host::addInventory(int id, int count) {
    for (auto& sl : inventory_) if (sl.id == id) { sl.count += count; return; }
    inventory_.push_back({ id, count });
}

void Host::buildEquipCandidates(int slot) {
    equipCand_.clear();
    for (int i = 0; i < (int)inventory_.size(); ++i) {
        auto it = items_.find(inventory_[i].id);
        if (it != items_.end() && slotAcceptsItem(slot, it->second)) equipCand_.push_back(i);
    }
}

void Host::swapEquip(int slot, int pick) {           // pick 0 = Remove; 1.. = equipCand_[pick-1]
    if (gameParty_.empty() || slot < 0 || slot > 5) return;
    GameMember& gm = gameParty_[curMember()];
    int oldId = gm.equip[slot];
    if (pick == 0) {                                 // unequip
        if (oldId > 0) { addInventory(oldId); gm.equip[slot] = 0; menuMsg_ = "Unequipped"; }
        else menuMsg_ = "(empty)";
        return;
    }
    int ci = pick - 1;
    if (ci < 0 || ci >= (int)equipCand_.size()) return;
    int invIdx = equipCand_[ci];
    if (invIdx < 0 || invIdx >= (int)inventory_.size()) return;
    int newId = inventory_[invIdx].id;
    gm.equip[slot] = newId;
    if (--inventory_[invIdx].count <= 0) inventory_.erase(inventory_.begin() + invIdx);
    if (oldId > 0) addInventory(oldId);              // old gear returns to the bag
    menuMsg_ = (items_.count(newId) ? items_[newId].name : "Item") + " equipped";
}

void Host::useItem(int invIdx) {
    if (invIdx < 0 || invIdx >= (int)inventory_.size()) return;
    InvSlot& slot = inventory_[invIdx];
    auto it = items_.find(slot.id);
    if (it == items_.end()) { menuMsg_ = "..."; return; }
    const Item& itm = it->second;
    bool consumable = (itm.type == 0);                   // item_type 0 = consumable/key item
    if (!consumable) { menuMsg_ = "Can't use that here."; return; }
    const std::string& d = itm.desc;
    if (d.find("Knocked Out") != std::string::npos) {           // revive item (Phoenix Down)
        int tgt = -1; for (int i = 0; i < (int)gameParty_.size(); ++i) if (gameParty_[i].hp <= 0) { tgt = i; break; }
        if (tgt < 0) { menuMsg_ = "No KO'd ally."; return; }
        gameParty_[tgt].hp = std::max(1, memberMaxHp(tgt) / 4);
        menuMsg_ = chars_[gameParty_[tgt].charIdx].name + " revived!";
        if (--slot.count <= 0) { inventory_.erase(inventory_.begin() + invIdx);
                                 if (pageCursor_ >= (int)inventory_.size()) pageCursor_ = std::max(0, (int)inventory_.size() - 1); }
        return;
    }
    int amt = 0; { size_t i = 0; while (i < d.size() && !std::isdigit((unsigned char)d[i])) ++i;
                   while (i < d.size() && std::isdigit((unsigned char)d[i])) amt = amt * 10 + (d[i++] - '0'); }
    bool mp = (d.find("MP") != std::string::npos && d.find("HP") == std::string::npos);
    if (amt <= 0) amt = mp ? 50 : 100;                   // Potion-tier default
    int tgt = -1; double lo = 2.0;                        // most-wounded living member
    for (int i = 0; i < (int)gameParty_.size(); ++i) {
        if (gameParty_[i].hp <= 0) continue;
        double r = mp ? (double)gameParty_[i].mp / std::max(1, memberMaxMp(i))
                      : (double)gameParty_[i].hp / std::max(1, memberMaxHp(i));
        if (r < lo) { lo = r; tgt = i; }
    }
    if (tgt < 0) { menuMsg_ = "No valid target."; return; }
    if (mp) gameParty_[tgt].mp = std::min(memberMaxMp(tgt), gameParty_[tgt].mp + amt);
    else    gameParty_[tgt].hp = std::min(memberMaxHp(tgt), gameParty_[tgt].hp + amt);
    std::string nm = chars_[gameParty_[tgt].charIdx].name;
    menuMsg_ = nm + "  +" + std::to_string(amt) + (mp ? " MP" : " HP");
    if (--slot.count <= 0) { inventory_.erase(inventory_.begin() + invIdx);
                             if (pageCursor_ >= (int)inventory_.size()) pageCursor_ = std::max(0, (int)inventory_.size() - 1); }
}

void Host::startBattle(int leadId) {
    enemies_.clear(); rewardsGiven_ = false; floats_.clear();
    const Monster* lead = nullptr;
    if (leadId >= 0) for (const auto& m : monsters_) if (m.id == leadId) { lead = &m; break; }
    if (!lead && !monsters_.empty()) {                       // random encounter -> a weak-ish lead
        std::vector<const Monster*> weak;
        for (const auto& m : monsters_) if (m.atk > 0 && m.atk <= 20) weak.push_back(&m);
        if (weak.empty()) for (const auto& m : monsters_) weak.push_back(&m);
        lead = weak[std::rand() % (int)weak.size()];
    }
    if (!lead) return;
    if (field_ && field_->map() && field_->map()->battle_bgm != 255)
        audio_.playBgm(field_->map()->battle_bgm);   // battle theme; field BGM restored on endBattle (poll)
    auto push = [&](const Monster* m) {
        // Decoded enemy mapping (SetBtlEnemyParam c:88427): W = weapon-attack
        // (body[15], baked as m->atk), A = LEVEL, D = body[18], SPD-class = level.
        Combatant e; e.name = m->name; e.hp = e.maxhp = m->hp;
        e.wpn = std::max(1, m->atk);
        e.atk = std::max(1, m->level);
        e.def = std::max(0, m->def);
        e.level = m->level; e.spd = std::max(1, m->level);
        e.mp = e.maxmp = m->hp / 8;
        e.exp = m->exp; e.gil = m->gil;
        e.sprite = m->sprite; e.frames = m->frames;
        enemies_.push_back(e);
    };
    push(lead);
    // Group extras = monsters of SIMILAR difficulty to the lead (attack band + hp cap), so a
    // Goblin pack never pulls in a Mud Golem.  The monster `level` field is not a reliable
    // difficulty proxy (e.g. Werewolf atk 205 is "level 1"); the stats are.
    int loA = std::max(1, lead->atk / 2), hiA = std::max(lead->atk * 9 / 5, lead->atk + 6);
    int hiH = lead->hp * 5 / 2 + 10;
    std::vector<const Monster*> pool;
    for (const auto& m : monsters_) if (m.atk >= loA && m.atk <= hiA && m.hp <= hiH) pool.push_back(&m);
    int extra = std::rand() % 3;                              // 0..2 more -> a group of 1..3
    for (int i = 0; i < extra && !pool.empty(); ++i) push(pool[std::rand() % (int)pool.size()]);
    std::unordered_map<std::string, int> total, idx;          // suffix dup names: Goblin A / B
    for (auto& e : enemies_) total[e.name]++;
    for (auto& e : enemies_) {
        std::string base = e.name;
        if (total[base] > 1) { e.name = base + " " + std::string(1, char('A' + idx[base])); idx[base]++; }
    }
    party_.clear();
    if (gameParty_.empty()) newGame();
    for (size_t gi = 0; gi < gameParty_.size(); ++gi) {
        const GameMember& gm = gameParty_[gi];
        if (gm.charIdx < 0 || gm.charIdx >= (int)chars_.size()) continue;
        const CharRec& c = chars_[gm.charIdx];
        int weaponAtk = (gm.equip[0] > 0 && items_.count(gm.equip[0])) ? items_[gm.equip[0]].atk : 0;
        int armorDef = 0;
        for (int k = 0; k < 6; ++k) if (gm.equip[k] > 0 && items_.count(gm.equip[k])) armorDef += items_[gm.equip[k]].def;
        Combatant cb;
        cb.name = c.name;
        cb.maxhp = (c.hp > 0 ? c.hp : 28 + c.level * 2 + c.vit * 4);
        cb.hp = std::max(0, std::min(gm.hp, cb.maxhp));     // CURRENT hp (carries between battles)
        cb.maxmp = c.mp; cb.mp = std::max(0, std::min(gm.mp, cb.maxmp));
        cb.atk = memberStat((int)gi, 0);                    // A = job-derived STR (SetJobStatus)
        cb.wpn = std::max(3, weaponAtk);                    // W = equipped weapon ATK
        cb.def = std::max(1, memberStat((int)gi, 2) + armorDef + c.level / 2);  // D = job-derived VIT + armor
        cb.level = c.level; cb.spd = std::max(1, memberStat((int)gi, 1));       // SPD job-derived
        cb.intl = memberStat((int)gi, 3); cb.mnd = memberStat((int)gi, 4);      // INT/MND job-derived
        party_.push_back(cb);
    }
    if (party_.empty()) { Combatant cb; cb.name = "Hero"; cb.hp = cb.maxhp = 34; cb.atk = 13; cb.wpn = 6; cb.def = 6; party_.push_back(cb); }
    target_ = firstLivingEnemy(0); enemyActor_ = 0;
    for (auto& c : party_)   c.atb = std::rand() % 256;     // stagger initial ATB gauges
    for (auto& e : enemies_) e.atb = std::rand() % 256;
    btlCmd_ = 0; btlMsg_.clear();
    mode_ = Mode::Battle;
    beginNextTurn();                                        // ATB chooses who acts first
    std::printf("[FFSmith] battle: %zu enemies (lead %s) vs %zu party\n",
                enemies_.size(), enemies_.empty() ? "?" : enemies_[0].name.c_str(), party_.size());
}

void Host::updateBattle(const InputState& in) {
    auto prevLivingEnemy = [&](int from) {
        for (int i = from - 1; i >= 0; --i) if (enemies_[i].hp > 0) return i;
        for (int i = (int)enemies_.size() - 1; i >= 0; --i) if (enemies_[i].hp > 0) return i;
        return from;
    };
    if (btlPhase_ == 0) {                                    // command: Attack / Magic / Defend / Run
        if (in.pressed & BTN_UP)   btlCmd_ = (btlCmd_ + 3) % 4;
        if (in.pressed & BTN_DOWN) btlCmd_ = (btlCmd_ + 1) % 4;
        if (in.pressed & BTN_CONFIRM) {
            if (btlCmd_ == 0) {                              // Attack -> target (auto if one)
                pendingSpell_ = -1;
                target_ = firstLivingEnemy(0);
                int living = 0; for (const auto& e : enemies_) if (e.hp > 0) living++;
                if (living <= 1) {
                    int d = physDamage(party_[btlMember_], enemies_[target_]);
                    enemies_[target_].hp = std::max(0, enemies_[target_].hp - d);
                    btlMsg_ = party_[btlMember_].name + " -> " + enemies_[target_].name + "   " + std::to_string(d) + " dmg";
                    btlPhase_ = 1;
                } else btlPhase_ = 5;
            } else if (btlCmd_ == 1) {                       // Magic
                buildSpellList();
                if (!spellList_.empty()) { btlSpellSel_ = 0; btlPhase_ = 6; }
                else { btlMsg_ = party_[btlMember_].name + " has no spell to cast."; btlPhase_ = 1; }
            } else if (btlCmd_ == 2) {                       // Defend
                party_[btlMember_].defending = true; btlMsg_ = party_[btlMember_].name + " defends."; btlPhase_ = 1;
            } else {                                         // Run
                if (!battleNoEscape_ && std::rand() % 2 == 0) {
                    battleOutcome_ = 2; endBattle(); return;   // escaped
                }
                btlMsg_ = "Couldn't escape!"; btlPhase_ = 1;
            }
        }
    } else if (btlPhase_ == 6) {                             // spell select
        int n = (int)spellList_.size();
        if (n > 0) {
            if (in.pressed & BTN_UP)   btlSpellSel_ = (btlSpellSel_ + n - 1) % n;
            if (in.pressed & BTN_DOWN) btlSpellSel_ = (btlSpellSel_ + 1) % n;
        }
        if (in.pressed & BTN_CANCEL) btlPhase_ = 0;
        if ((in.pressed & BTN_CONFIRM) && n > 0) {
            pendingSpell_ = spellList_[btlSpellSel_];
            if (spells_[pendingSpell_].type == 0) { target_ = firstLivingEnemy(0); btlPhase_ = 5; }
            else {
                int lo = -1, loHp = 1 << 30;
                for (int i = 0; i < (int)party_.size(); ++i)
                    if (party_[i].hp > 0 && party_[i].hp < loHp) { loHp = party_[i].hp; lo = i; }
                castOn(0, lo >= 0 ? lo : btlMember_);
            }
        }
        } else if (btlPhase_ == 5) {                             // target selection
        if (in.pressed & (BTN_RIGHT | BTN_DOWN)) { int t = firstLivingEnemy(target_ + 1); target_ = (t >= 0) ? t : firstLivingEnemy(0); }
        if (in.pressed & (BTN_LEFT | BTN_UP))    target_ = prevLivingEnemy(target_);
        if (in.pressed & BTN_CANCEL) btlPhase_ = 0;
        if (in.pressed & BTN_CONFIRM) {
            if (pendingSpell_ >= 0) { castOn(1, target_); }
            else {
                int d = physDamage(party_[btlMember_], enemies_[target_]);
                enemies_[target_].hp = std::max(0, enemies_[target_].hp - d);
                btlMsg_ = party_[btlMember_].name + " -> " + enemies_[target_].name + "   " + std::to_string(d) + " dmg";
                btlPhase_ = 1;
            }
        }
    } else if (btlPhase_ == 1) {                             // resolve player action
        if (in.pressed & BTN_CONFIRM) {
            if (!enemiesAlive()) { btlMsg_ = awardBattleRewards(); battleOutcome_ = 1; btlPhase_ = 3; return; }
            beginNextTurn();
        }
    } else if (btlPhase_ == 2) {                             // resolve enemy action
        if (in.pressed & BTN_CONFIRM) {
            if (!partyAlive()) { btlMsg_ = "Party wiped out..."; battleOutcome_ = 0; btlPhase_ = 4; return; }
            beginNextTurn();
        }
    } else {                                                 // 3 win / 4 lose
        if (in.pressed & BTN_CONFIRM) endBattle();
    }
}

void Host::renderBattle() {
    int winW = 0, winH = 0; SDL_GetWindowSize(window_, &winW, &winH);
    const int sc = cfg_.scale < 1 ? 1 : cfg_.scale;
    int vw = winW / sc; if (vw < 16) vw = 16;
    int vh = winH / sc; if (vh < 16) vh = 16;
    SDL_RenderSetScale(renderer_, (float)sc, (float)sc);
    int sceneH = vh - (fch_ * 5);
    if (btlbgTex_) { SDL_Rect dst{ 0, 0, vw, sceneH }; SDL_RenderCopy(renderer_, btlbgTex_, nullptr, &dst); }
    else { SDL_SetRenderDrawColor(renderer_, 26, 18, 38, 255); SDL_Rect r{ 0, 0, vw, sceneH }; SDL_RenderFillRect(renderer_, &r); }
    SDL_SetRenderDrawColor(renderer_, 8, 10, 28, 255); SDL_Rect bp{ 0, sceneH, vw, vh - sceneH }; SDL_RenderFillRect(renderer_, &bp);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    int N = (int)enemies_.size(); if (N < 1) N = 1;
    ++battleAnim_;
    int slotW = vw / N;
    int spriteBottom = sceneH * 72 / 100;             // monsters stand in the upper scene
    for (int i = 0; i < (int)enemies_.size(); ++i) {
        const Combatant& e = enemies_[i];
        bool dead = e.hp <= 0, tgt = (btlPhase_ == 5 && i == target_);
        int cx = i * slotW + slotW / 2;
        SDL_Rect dst{ cx - 16, spriteBottom - 24, 32, 24 };   // fallback rect if no sprite
        int tw = 0, th = 0;
        SDL_Texture* mt = (e.sprite >= 0 && e.sprite < 0xffff) ? monTex(e.sprite, e.frames, tw, th) : nullptr;
        if (mt && tw > 0 && th > 0) {
            double maxw = std::min(slotW - 6, 112), maxh = sceneH * 0.7;
            double scl = std::min(maxw / tw, maxh / th); if (scl > 2.0) scl = 2.0; if (scl <= 0) scl = 1.0;
            int dw = (int)(tw * scl), dh = (int)(th * scl);
            int bob = (battleAnim_ / 6 + i) % 4; if (bob > 2) bob = 4 - bob;   // gentle idle bob (0,1,2,1), no flicker
            dst = SDL_Rect{ cx - dw / 2, spriteBottom - dh + bob, dw, dh };
            SDL_SetTextureColorMod(mt, dead ? 90 : 255, dead ? 90 : 255, dead ? 90 : 255);
            SDL_SetTextureAlphaMod(mt, dead ? 110 : 255);
            SDL_RenderCopy(renderer_, mt, nullptr, &dst);   // whole image (single static sprite)
            SDL_SetTextureColorMod(mt, 255, 255, 255); SDL_SetTextureAlphaMod(mt, 255);
        }
        if (tgt) {                                    // target marker: highlight box + a small caret above
            SDL_SetRenderDrawColor(renderer_, 255, 240, 120, 255); SDL_RenderDrawRect(renderer_, &dst);
            for (int t2 = 0; t2 < 4; ++t2) { SDL_Rect tk{ cx - t2, dst.y - 6 + t2, 1 + t2 * 2, 1 }; SDL_RenderFillRect(renderer_, &tk); }
        }
        // name + HP bar under the sprite (still in the scene)
        int ny = spriteBottom + 1;
        drawText(cx - (int)e.name.size() * fcw_ / 2, ny, e.name, slotW / (fcw_ > 0 ? fcw_ : 8),
                 dead ? 120 : 255, dead ? 120 : 230, dead ? 120 : 210);
        int barW = std::min(slotW - 8, 60), bxw = cx - barW / 2, byy = ny + fch_ + 1;
        SDL_SetRenderDrawColor(renderer_, 60, 30, 30, 220); SDL_Rect bgr{ bxw, byy, barW, 3 }; SDL_RenderFillRect(renderer_, &bgr);
        int fillw = e.maxhp > 0 ? barW * std::max(0, e.hp) / e.maxhp : 0;
        SDL_SetRenderDrawColor(renderer_, 220, 64, 64, 255); SDL_Rect bf{ bxw, byy, fillw, 3 }; SDL_RenderFillRect(renderer_, &bf);
    }
    int py = sceneH + 3;
    for (size_t i = 0; i < party_.size(); ++i) {
        const Combatant& c = party_[i];
        int ty = py + (int)i * fch_;
        if (btlPhase_ == 0 && (int)i == btlMember_) drawText(2, ty, ">", 2, 255, 240, 120);
        std::string ln = c.name + " " + std::to_string(std::max(0, c.hp)) + "/" + std::to_string(c.maxhp) + (c.maxmp > 0 ? "  M" + std::to_string(c.mp) : "");
        Uint8 g = c.hp > 0 ? 230 : 110, b = c.hp > 0 ? 255 : 110;
        drawText(10, ty, ln, vw / 2 / (fcw_ > 0 ? fcw_ : 8), 235, g, b);
        int gW = 26, gX = vw / 2 - gW - 4;             // inline HP/MP gauges
        drawGauge(gX, ty + 1, gW, 2, c.hp, c.maxhp, false);
        if (c.maxmp > 0) drawGauge(gX, ty + 4, gW, 2, c.mp, c.maxmp, true);
    }
    int rx = vw / 2 + 2, ry = sceneH + 2, rw = vw / 2 - 6, rh = vh - sceneH - 4;
    drawWindow(rx, ry, rw, rh, 235);               // FFD-skinned battle command window
    if (btlPhase_ == 0) {
        static const char* cmd[4] = { "Attack", "Magic", "Defend", "Run" };
        for (int i = 0; i < 4; ++i) {
            int ty = ry + 3 + i * fch_;
            if (i == btlCmd_) drawText(rx + 4, ty, ">", 2, 255, 240, 120);
            drawText(rx + 13, ty, cmd[i], 10, 255, 255, 255);
        }
    } else if (btlPhase_ == 6) {
        int start = std::max(0, btlSpellSel_ - 4);
        for (int i = start; i < (int)spellList_.size() && i < start + 5; ++i) {
            const Spell& sp = spells_[spellList_[i]];
            int ty = ry + 3 + (i - start) * fch_;
            if (i == btlSpellSel_) drawText(rx + 3, ty, ">", 2, 255, 240, 120);
            drawText(rx + 11, ty, sp.name + " " + std::to_string(sp.mp), rw / (fcw_ > 0 ? fcw_ : 8) - 1, 200, 220, 255);
        }
    } else if (btlPhase_ == 5) {
        drawText(rx + 6, ry + 5, "Target:", 12, 235, 235, 200);
        drawText(rx + 6, ry + 5 + fch_, (target_ >= 0 && target_ < (int)enemies_.size()) ? enemies_[target_].name : "?",
                 rw / (fcw_ > 0 ? fcw_ : 8) - 1, 255, 240, 160);
    } else {
        drawText(rx + 6, ry + 5, btlMsg_, rw / (fcw_ > 0 ? fcw_ : 8) - 1, 235, 235, 200);
        drawText(rx + rw - 3 * fcw_ - 5, ry + rh - fch_ - 3, "-Z-", 4, 150, 160, 200);
    }
    // floating damage / heal numbers: rise and fade, then expire
    {
        int fn = (int)enemies_.size(); if (fn < 1) fn = 1;
        int eg = 4, ebw = (vw - eg * (fn + 1)) / fn; if (ebw < 40) ebw = 40;
        for (auto it = floats_.begin(); it != floats_.end(); ) {
            int cx, cy;
            if (it->enemy) { int i = it->idx < 0 ? 0 : it->idx; cx = eg + i * (ebw + eg) + ebw / 2; cy = 18; }
            else { cx = vw / 4; cy = sceneH + 3 + it->idx * fch_; }
            cy -= it->age / 2 + 2;
            std::string t = (it->heal ? "+" : "") + std::to_string(it->value);
            int br = 255 - (it->age * 5 < 170 ? it->age * 5 : 170);
            Uint8 r, g, b;
            if (it->heal) { r = 130; g = (Uint8)(br < 150 ? 150 : br); b = 130; }
            else          { r = (Uint8)(br < 170 ? 170 : br); g = (Uint8)(br < 120 ? 120 : br - 30); b = 90; }
            drawText(cx - (int)t.size() * fcw_ / 2, cy, t, 8, r, g, b);
            ++it->age;
            if (it->age > 36) it = floats_.erase(it); else ++it;
        }
    }
}

void Host::debugOpenMagic() {
    btlMember_ = 0;
    if (!party_.empty()) { buildSpellList(); btlSpellSel_ = 0; btlPhase_ = 6; curIsEnemy_ = false; }
}

int Host::simBattle(int monsterId) {
    startBattle(monsterId);
    InputState c; c.pressed = BTN_CONFIRM;
    int guard = 0; std::string last;
    while (mode_ == Mode::Battle && guard++ < 800) {
        if (!btlMsg_.empty() && btlMsg_ != last) {
            int liveE = 0; for (const auto& e : enemies_) if (e.hp > 0) liveE++;
            std::printf("  [sim] enemies_alive=%d  %s\n", liveE, btlMsg_.c_str());
            last = btlMsg_;
        }
        updateBattle(c);
    }
    std::printf("  [sim] -> %s in %d steps (partyAlive=%d, enemiesAlive=%d)\n",
                (partyAlive() && !enemiesAlive()) ? "VICTORY" : "DEFEAT", guard, (int)partyAlive(), (int)enemiesAlive());
    std::printf("  [sim] persistent party:");
    for (size_t i = 0; i < gameParty_.size(); ++i)
        std::printf(" %s=%d/%d(L%d,%dxp)", chars_[gameParty_[i].charIdx].name.c_str(), gameParty_[i].hp, memberMaxHp((int)i), gameParty_[i].level, gameParty_[i].exp);
    std::printf("\n");
    return partyAlive() ? 1 : 0;
}

bool Host::shotField(const std::string& path) {
    render();
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer_, &w, &h);
    if (w <= 0 || h <= 0) return false;
    Texture t; t.w = w; t.h = h; t.rgba.assign((size_t)w * h * 4, 0);
    if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_RGBA32, t.rgba.data(), w * 4) != 0) {
        std::fprintf(stderr, "[FFSmith] RenderReadPixels failed: %s\n", SDL_GetError());
        return false;
    }
    return save_tex(path, t);
}

bool Host::frame() {
    if (!started_) { started_ = true; running_ = true; prevCtr_ = SDL_GetPerformanceCounter(); acc_ = 0.0; }
    if (!running_) return false;
    pumpEvents();
    const double dt = 1.0 / static_cast<double>(cfg_.tick_hz);
    if (cfg_.max_frames >= 0) {
        stepInput(); update(dt); render();
    } else {
        const uint64_t now = SDL_GetPerformanceCounter();
        acc_ += static_cast<double>(now - prevCtr_) / static_cast<double>(SDL_GetPerformanceFrequency());
        prevCtr_ = now;
        if (acc_ > 0.25) acc_ = 0.25;
        while (acc_ >= dt && running_) { stepInput(); update(dt); acc_ -= dt; }
        render();
    }
    return running_;
}

int Host::run() {
    started_ = false;
    while (frame()) {}
    std::printf("[FFSmith] clean exit after %llu ticks\n", static_cast<unsigned long long>(tick_count_));
    return 0;
}

}  // namespace ffsmith
