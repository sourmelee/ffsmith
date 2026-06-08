#include "host/host.h"

#include <SDL.h>
#include <cstdio>
#include <string>
#include <algorithm>
#include <cstdlib>
#include "field/field.h"
#include "field/event_vm.h"

namespace ffsmith {

namespace { const char* kMenuItems[] = {"Item", "Equip", "Status", "Save", "Quit"}; const int kMenuN = 5; }

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

void Host::update(double /*dt*/) {
    if (mode_ == Mode::Debug) {
        updateDebug(input_);
    } else if (mode_ == Mode::Battle) {
        updateBattle(input_);
    } else if (mode_ == Mode::Title) {
        ++blink_;
        if (input_.pressed & (BTN_CONFIRM | BTN_MENU)) { mode_ = Mode::Debug; blink_ = 0; }
    } else if (menuOpen_) {
        updateMenu(input_);
    } else if (field_) {
        if (input_.pressed & BTN_MENU) { menuOpen_ = true; menuCursor_ = 0; }
        else field_->update(input_);
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
                case 0: menuPage_ = 1; pageCursor_ = 0; pageScroll_ = 0; break;  // Item
                case 1: menuPage_ = 2; pageChar_ = 0; break;                      // Equip
                case 2: menuPage_ = 3; pageChar_ = 0; break;                      // Status
                case 4: running_ = false; break;                                  // Quit
                default: break;                                                   // Save (stub)
            }
        }
    } else if (menuPage_ == 1) {                            // Item list
        int n = (int)itemIds_.size();
        if (n > 0) {
            if (in.pressed & BTN_UP)   pageCursor_ = (pageCursor_ + n - 1) % n;
            if (in.pressed & BTN_DOWN) pageCursor_ = (pageCursor_ + 1) % n;
            if (in.pressed & BTN_L)    pageCursor_ = std::max(0, pageCursor_ - 10);
            if (in.pressed & BTN_R)    pageCursor_ = std::min(n - 1, pageCursor_ + 10);
        }
        if (in.pressed & (BTN_CANCEL | BTN_MENU)) menuPage_ = 0;
    } else {                                                // Equip / Status: cycle character
        int n = (int)chars_.size();
        if (n > 0) {
            if (in.pressed & (BTN_DOWN | BTN_RIGHT)) pageChar_ = (pageChar_ + 1) % n;
            if (in.pressed & (BTN_UP | BTN_LEFT))    pageChar_ = (pageChar_ + n - 1) % n;
        }
        if (in.pressed & (BTN_CANCEL | BTN_MENU)) menuPage_ = 0;
    }
}

void Host::render() {
    if (mode_ == Mode::Debug) { renderDebug(); SDL_RenderPresent(renderer_); return; }
    if (mode_ == Mode::Battle) { renderBattle(); SDL_RenderPresent(renderer_); return; }
    if (mode_ == Mode::Title) { renderTitle(); SDL_RenderPresent(renderer_); return; }
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
    if (((blink_ / 32) & 1) == 0 && fcw_ > 0) {
        const char* p = "PRESS  START";
        drawText(vw / 2 - 12 * fcw_ / 2, vh * 3 / 4, p, 40, 235, 235, 255);
    }
}

void Host::renderMenu(int vw, int vh) {
    if (menuPage_ != 0) {
        int px = 6, py = 6, pw = vw - 12, ph = vh - 12;
        SDL_SetRenderDrawColor(renderer_, 10, 14, 40, 242);
        SDL_Rect box{ px, py, pw, ph }; SDL_RenderFillRect(renderer_, &box);
        SDL_SetRenderDrawColor(renderer_, 235, 235, 255, 255);
        SDL_RenderDrawRect(renderer_, &box);
        if (menuPage_ == 1) renderItemPage(px, py, pw, ph);
        else                renderCharPage(px, py, pw, ph, menuPage_ == 3);
        drawText(px + pw - 7 * fcw_ - 4, py + ph - fch_ - 2, "X:back", 8, 150, 160, 200);
        return;
    }
    const int pw = 96, ph = kMenuN * 14 + 12, px = vw - pw - 6, py = 6;
    SDL_SetRenderDrawColor(renderer_, 12, 16, 48, 238);
    SDL_Rect box{ px, py, pw, ph }; SDL_RenderFillRect(renderer_, &box);
    SDL_SetRenderDrawColor(renderer_, 235, 235, 255, 255);
    SDL_RenderDrawRect(renderer_, &box);
    for (int i = 0; i < kMenuN; ++i) {
        int ty = py + 6 + i * 14;
        if (i == menuCursor_) drawText(px + 5, ty, ">", 2, 255, 240, 120);
        drawText(px + 16, ty, kMenuItems[i], 12, 255, 255, 255);
    }
}

void Host::renderItemPage(int px, int py, int pw, int ph) {
    drawText(px + 8, py + 4, "ITEMS", 40, 255, 235, 120);
    int n = (int)itemIds_.size();
    int maxChars = fcw_ > 0 ? (pw - 20) / fcw_ : 30;
    int rowH = fch_ > 0 ? fch_ : 14;
    int descH = rowH * 3 + 6;
    int listTop = py + 20, listBot = py + ph - descH - 4;
    int visRows = (listBot - listTop) / rowH; if (visRows < 1) visRows = 1;
    if (pageCursor_ < pageScroll_) pageScroll_ = pageCursor_;
    if (pageCursor_ >= pageScroll_ + visRows) pageScroll_ = pageCursor_ - visRows + 1;
    if (pageScroll_ < 0) pageScroll_ = 0;
    for (int i = 0; i < visRows && pageScroll_ + i < n; ++i) {
        int idx = pageScroll_ + i, id = itemIds_[idx];
        int ty = listTop + i * rowH;
        bool sel = (idx == pageCursor_);
        if (sel) drawText(px + 4, ty, ">", 2, 255, 240, 120);
        drawText(px + 14, ty, items_[id].name, maxChars, sel ? 255 : 215, 235, sel ? 170 : 255);
    }
    if (n > 0) {
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 175);
        SDL_Rect db{ px + 3, listBot, pw - 6, descH }; SDL_RenderFillRect(renderer_, &db);
        drawText(px + 8, listBot + 3, items_[itemIds_[pageCursor_]].desc, maxChars - 1, 200, 230, 255);
        std::string pos = std::to_string(pageCursor_ + 1) + "/" + std::to_string(n);
        drawText(px + pw - (int)pos.size() * fcw_ - 6, py + 4, pos, 12, 180, 190, 220);
    }
}

void Host::renderCharPage(int px, int py, int pw, int ph, bool status) {
    (void)ph;
    drawText(px + 8, py + 4, status ? "STATUS" : "EQUIP", 40, 255, 235, 120);
    if (chars_.empty()) { drawText(px + 8, py + 24, "(no character data)", 40, 220, 220, 220); return; }
    const CharRec& c = chars_[pageChar_ % (int)chars_.size()];
    int maxChars = fcw_ > 0 ? (pw - 20) / fcw_ : 30;
    std::string hdr = "<  " + c.name + "  >   (" + std::to_string(pageChar_ + 1) + "/" + std::to_string(chars_.size()) + ")";
    drawText(px + 8, py + 22, hdr, maxChars, 255, 255, 160);
    if (status) {
        drawText(px + 10, py + 42, "Lv " + std::to_string(c.level) + "   HP " + std::to_string(c.hp) + "   MP " + std::to_string(c.mp), maxChars, 235, 235, 255);
        drawText(px + 10, py + 42 + fch_ * 2,  "STR " + std::to_string(c.str) + "      SPD " + std::to_string(c.spd), maxChars, 230, 235, 255);
        drawText(px + 10, py + 42 + fch_ * 3,  "VIT " + std::to_string(c.vit) + "   INT " + std::to_string(c.intl) + "   MND " + std::to_string(c.mnd), maxChars, 230, 235, 255);
        int w0 = c.equip[0];
        std::string wpn = (w0 > 0 && items_.count(w0)) ? items_[w0].name : std::string("-");
        drawText(px + 10, py + 42 + fch_ * 5,  "Weapon: " + wpn, maxChars, 200, 220, 255);
    } else {
        for (int k = 0; k < 6; ++k) {
            int ty = py + 42 + k * (fch_ + 2);
            int id = c.equip[k];
            std::string nm = (id > 0 && items_.count(id)) ? items_[id].name
                           : (id ? ("#" + std::to_string(id)) : std::string("(empty)"));
            drawText(px + 10, ty, "Slot " + std::to_string(k + 1) + ":  " + nm, maxChars, 230, 230, 255);
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
    Texture bg = load_tex(dir + "/ui/btlbg.tex");
    if (bg.valid() && renderer_) {
        if (btlbgTex_) SDL_DestroyTexture(btlbgTex_);
        btlbgTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, bg.w, bg.h);
        if (btlbgTex_) { SDL_UpdateTexture(btlbgTex_, nullptr, bg.rgba.data(), bg.w * 4); SDL_SetTextureBlendMode(btlbgTex_, SDL_BLENDMODE_BLEND); }
    }
    std::printf("[FFSmith] menu data: %zu items, %zu chars, %zu monsters\n", items_.size(), chars_.size(), monsters_.size());
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
    out.map    = (dbgMapIdx_ >= 0 && dbgMapIdx_ < (int)dbgMaps_.size()) ? dbgMaps_[dbgMapIdx_] : std::string();
    out.img    = (dbgSprIdx_ >= 0 && dbgSprIdx_ < (int)dbgSprites_.size()) ? dbgSprites_[dbgSprIdx_] : -1;
    out.x = dbgX_; out.y = dbgY_; out.facing = dbgFacing_; out.noclip = dbgNoclip_;
    overlayOn_ = dbgOverlay_; hudOn_ = dbgHud_;     // apply the view toggles on launch
    return true;
}

void Host::updateDebug(const InputState& in) {
    const int N = 10;
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
        default: break;
    }
    if (conf && dbgRow_ == 9) dbgStart_ = true;
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
    std::string mapName = (dbgMapIdx_ < (int)dbgMaps_.size()) ? dbgMaps_[dbgMapIdx_] : std::string("(no maps)");
    int sprId = (dbgSprIdx_ < (int)dbgSprites_.size()) ? dbgSprites_[dbgSprIdx_] : -1;
    std::string rows[10] = {
        "Map:       < " + mapName + " >   (" + std::to_string(dbgMapIdx_ + 1) + "/" + std::to_string(dbgMaps_.size()) + ")",
        "Character: < fldchr" + std::to_string(sprId) + " >",
        "Spawn X:   < " + std::to_string(dbgX_) + " >",
        "Spawn Y:   < " + std::to_string(dbgY_) + " >",
        std::string("Facing:    < ") + fac[dbgFacing_ & 3] + " >",
        std::string("No-clip:   < ") + (dbgNoclip_ ? "On" : "Off") + " >",
        std::string("Collision: < ") + (dbgOverlay_ ? "On" : "Off") + " >",
        std::string("HUD:       < ") + (dbgHud_ ? "On" : "Off") + " >",
        std::string("Scale:     < ") + std::to_string(dbgScale_) + "x >",
        std::string("START"),
    };
    int x = 12, y = 10;
    drawText(x, y, "FFSmith  -  Debug Launcher", 60, 255, 235, 120);
    y += 22;
    for (int i = 0; i < 10; ++i) {
        int ry = y + i * 13 + (i == 9 ? 8 : 0);
        if (i == dbgRow_) drawText(x, ry, ">", 2, 255, 240, 120);
        bool start = (i == 9);
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

void Host::startBattle(int leadId) {
    enemies_.clear();
    const Monster* lead = nullptr;
    if (leadId >= 0) for (const auto& m : monsters_) if (m.id == leadId) { lead = &m; break; }
    if (!lead && !monsters_.empty()) {                       // random encounter -> a weak-ish lead
        std::vector<const Monster*> weak;
        for (const auto& m : monsters_) if (m.atk > 0 && m.atk <= 20) weak.push_back(&m);
        if (weak.empty()) for (const auto& m : monsters_) weak.push_back(&m);
        lead = weak[std::rand() % (int)weak.size()];
    }
    if (!lead) return;
    auto push = [&](const Monster* m) {
        Combatant e; e.name = m->name; e.hp = e.maxhp = m->hp;
        e.atk = std::max(1, m->atk);      // A = monster attack stat (BTLACT 0x3c)
        e.wpn = 5 + m->level;             // W = small innate weapon power (no equipment)
        e.def = std::max(1, m->def);      // D (BTLACT 0x58)
        e.level = m->level; e.spd = 7;
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
    int n = std::min((int)chars_.size(), 4);
    for (int i = 0; i < n; ++i) {
        const CharRec& c = chars_[i];
        int weaponAtk = (c.equip[0] > 0 && items_.count(c.equip[0])) ? items_[c.equip[0]].atk : 0;
        int armorDef = 0;
        for (int k = 0; k < 6; ++k) if (c.equip[k] > 0 && items_.count(c.equip[k])) armorDef += items_[c.equip[k]].def;
        Combatant cb;
        cb.name = c.name;
        cb.hp = cb.maxhp = (c.hp > 0 ? c.hp : 28 + c.level * 2 + c.vit * 4);  // real max-HP (growth table)
        cb.atk = std::max(1, c.str);                        // A = real STR (attack stat)
        cb.wpn = std::max(3, weaponAtk);                    // W = equipped weapon ATK
        cb.def = std::max(1, c.vit + armorDef + c.level / 2);
        cb.level = c.level; cb.spd = std::max(1, c.spd);
        cb.mp = cb.maxmp = c.mp; cb.intl = c.intl; cb.mnd = c.mnd;
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
                if (std::rand() % 2 == 0) { mode_ = Mode::Field; return; }
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
            if (!enemiesAlive()) { btlMsg_ = "Victory!"; btlPhase_ = 3; return; }
            beginNextTurn();
        }
    } else if (btlPhase_ == 2) {                             // resolve enemy action
        if (in.pressed & BTN_CONFIRM) {
            if (!partyAlive()) { btlMsg_ = "Party wiped out..."; btlPhase_ = 4; return; }
            beginNextTurn();
        }
    } else {                                                 // 3 win / 4 lose
        if (in.pressed & BTN_CONFIRM) mode_ = Mode::Field;
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
    int gap = 4, boxW = (vw - gap * (N + 1)) / N; if (boxW < 40) boxW = 40;
    for (int i = 0; i < (int)enemies_.size(); ++i) {
        const Combatant& e = enemies_[i];
        int bx = gap + i * (boxW + gap), by = 16, bh = fch_ + 12;
        bool dead = e.hp <= 0, tgt = (btlPhase_ == 5 && i == target_);
        SDL_SetRenderDrawColor(renderer_, tgt ? 60 : 0, 0, 0, dead ? 80 : 165);
        SDL_Rect eb{ bx, by, boxW, bh }; SDL_RenderFillRect(renderer_, &eb);
        if (tgt) { SDL_SetRenderDrawColor(renderer_, 255, 240, 120, 255); SDL_RenderDrawRect(renderer_, &eb); }
        drawText(bx + 4, by + 2, e.name, boxW / (fcw_ > 0 ? fcw_ : 8) - 1, dead ? 120 : 255, dead ? 120 : 210, dead ? 120 : 210);
        int barW = boxW - 8, byy = by + fch_ + 2;
        SDL_SetRenderDrawColor(renderer_, 60, 30, 30, 255); SDL_Rect bgr{ bx + 4, byy, barW, 3 }; SDL_RenderFillRect(renderer_, &bgr);
        int fillw = e.maxhp > 0 ? barW * std::max(0, e.hp) / e.maxhp : 0;
        SDL_SetRenderDrawColor(renderer_, 220, 64, 64, 255); SDL_Rect bf{ bx + 4, byy, fillw, 3 }; SDL_RenderFillRect(renderer_, &bf);
    }
    int py = sceneH + 3;
    for (size_t i = 0; i < party_.size(); ++i) {
        const Combatant& c = party_[i];
        int ty = py + (int)i * fch_;
        if (btlPhase_ == 0 && (int)i == btlMember_) drawText(2, ty, ">", 2, 255, 240, 120);
        std::string ln = c.name + " " + std::to_string(std::max(0, c.hp)) + "/" + std::to_string(c.maxhp) + (c.maxmp > 0 ? "  M" + std::to_string(c.mp) : "");
        Uint8 g = c.hp > 0 ? 230 : 110, b = c.hp > 0 ? 255 : 110;
        drawText(10, ty, ln, vw / 2 / (fcw_ > 0 ? fcw_ : 8), 235, g, b);
    }
    int rx = vw / 2 + 2, ry = sceneH + 2, rw = vw / 2 - 6, rh = vh - sceneH - 4;
    SDL_SetRenderDrawColor(renderer_, 12, 16, 48, 238); SDL_Rect cb{ rx, ry, rw, rh }; SDL_RenderFillRect(renderer_, &cb);
    SDL_SetRenderDrawColor(renderer_, 235, 235, 255, 255); SDL_RenderDrawRect(renderer_, &cb);
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
