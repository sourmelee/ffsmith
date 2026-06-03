#include "host/host.h"

#include <SDL.h>
#include <cstdio>
#include <string>
#include "field/field.h"
#include "field/event_vm.h"

namespace ffsmith {

Host::Host(const HostConfig& cfg) : cfg_(cfg) {}

Host::~Host() {
    for (auto& kv : sprites_) if (kv.second) SDL_DestroyTexture(kv.second);
    if (fontTex_)  SDL_DestroyTexture(fontTex_);
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

void Host::setField(Field* f, const Texture& mapImg) {
    if (!ensureMapTexture(mapImg)) return;
    field_ = f;
    SDL_RenderSetLogicalSize(renderer_, 0, 0);
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

// Draw a character frame, feet-aligned on a tile at logical (lx,ly).
// field_anm character template (Jack-confirmed on fldchr1): 48x48 cells, origin
// (1,1), pitch 50.  ROW = facing: DOWN=y1, UP=y51, LEFT=y101, RIGHT=y101 (LEFT
// flipped horizontally).  COL = frame: idle=x1, walkA=x51, walkB=x101.
// Small NPC sheets fall back to the top-left cell.
bool Host::drawSprite(int img, int var, int facing, int animCol, int lx, int ly, int tile) {
    int tw = 0, th = 0;
    SDL_Texture* tex = spriteTex(img, var, tw, th);
    if (!tex) return false;
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
void Host::drawText(int x, int y, const std::string& s, int maxChars, uint8_t r, uint8_t g, uint8_t b) {
    if (!fontTex_ || fcw_ <= 0 || maxChars < 1) return;
    SDL_SetTextureColorMod(fontTex_, r, g, b);
    int cx = 0, cy = 0;
    auto put = [&](unsigned char c) {
        if (c < (unsigned)ffirst_ || c >= (unsigned)(ffirst_ + 95)) c = '?';
        int gi = (int)c - ffirst_;
        SDL_Rect src{ (gi % fcols_) * fcw_, (gi / fcols_) * fch_, fcw_, fch_ };
        SDL_Rect dst{ x + cx * fcw_, y + cy * fch_, fcw_, fch_ };
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
}

static uint32_t keyToButton(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP: case SDLK_w: return BTN_UP;
        case SDLK_DOWN: case SDLK_s: return BTN_DOWN;
        case SDLK_LEFT: case SDLK_a: return BTN_LEFT;
        case SDLK_RIGHT: case SDLK_d: return BTN_RIGHT;
        case SDLK_z: case SDLK_SPACE: case SDLK_RETURN: return BTN_CONFIRM;
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

void Host::update(double /*dt*/) {
    if (field_) field_->update(input_);
    ++tick_count_;
    if (cfg_.max_frames >= 0 && static_cast<int>(tick_count_) >= cfg_.max_frames) running_ = false;
}

void Host::render() {
    if (field_ && map_tex_) {
        int winW = 0, winH = 0;
        SDL_GetWindowSize(window_, &winW, &winH);
        const int sc = cfg_.scale < 1 ? 1 : cfg_.scale;
        int vw = winW / sc; if (vw < 16) vw = 16;
        int vh = winH / sc; if (vh < 16) vh = 16;
        SDL_RenderSetScale(renderer_, (float)sc, (float)sc);

        const int tile = field_->tile();
        const int px = field_->pixelX(), py = field_->pixelY();
        int camX = px + tile / 2 - vw / 2, camY = py + tile / 2 - vh / 2;
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

        // NPC sprites (or faint marker for script-only triggers)
        for (const auto& e : field_->map()->events) {
            int lx = offX + e.x * tile - camX, ly = offY + e.y * tile - camY;
            if (e.img > 0) {
                if (!drawSprite(e.img, e.var, FACE_DOWN, 0, lx, ly, tile)) {
                    SDL_SetRenderDrawColor(renderer_, 80, 170, 255, 200);
                    SDL_Rect er{ lx, ly, tile, tile }; SDL_RenderFillRect(renderer_, &er);
                }
            } else if (!e.scripts.empty()) {
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

        if (field_->inDialogue()) {
            const int boxH = 68, by = vh - boxH - 4, bx = 6, bw = vw - 12;
            SDL_SetRenderDrawColor(renderer_, 12, 16, 48, 235);
            SDL_Rect box{ bx, by, bw, boxH }; SDL_RenderFillRect(renderer_, &box);
            SDL_SetRenderDrawColor(renderer_, 235, 235, 255, 255);
            SDL_RenderDrawRect(renderer_, &box);
            const int id = field_->dialogueMsg();
            auto it = messages_.find(id);
            std::string txt = (it != messages_.end()) ? it->second
                                                       : ("[msg " + std::to_string(id) + "]");
            const int maxChars = fcw_ > 0 ? (bw - 12) / fcw_ : 30;
            drawText(bx + 6, by + 6, txt, maxChars, 255, 255, 255);
        }
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
