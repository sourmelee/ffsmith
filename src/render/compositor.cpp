#include "render/compositor.h"

#include <cstdio>
#include <unordered_map>

namespace ffsmith {

// PIL's DIV255 rounding (libImaging): tmp = a + 0x80; (tmp>>8 + tmp) >> 8.
// Reproduced so semi-transparent pixels match Pillow's alpha_composite.
static inline int div255(int a) {
    int tmp = a + 0x80;
    return ((tmp >> 8) + tmp) >> 8;
}

// Load slot tilesheet with the engine's variant fallback (mc{N}_{V} then
// mc{N}_0), cached. Returns nullptr if mc < 0 or no file.
static Texture* load_slot(const std::string& bundle_dir,
                          std::unordered_map<int, Texture>& cache,
                          int mc, int var) {
    if (mc < 0) return nullptr;
    int key = mc * 100 + var;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second.valid() ? &it->second : nullptr;
    Texture t;
    int variants[2] = {var, 0};
    for (int vi = 0; vi < 2; ++vi) {
        char name[512];
        std::snprintf(name, sizeof(name), "%s/tex/mc%d_%d.tex",
                      bundle_dir.c_str(), mc, variants[vi]);
        t = load_tex(name);
        if (t.valid()) break;
    }
    Texture& slot = cache[key];
    slot = std::move(t);
    return slot.valid() ? &slot : nullptr;
}

Texture compose_map(const std::string& bundle_dir, const FfMap& map) {
    return compose_range(bundle_dir, map, 0, (int)map.layers.size(), true);
}

Texture compose_range(const std::string& bundle_dir, const FfMap& map, int lo, int hi, bool opaque) {
    Texture out;
    if (!map.valid()) return out;
    if (lo < 0) lo = 0;
    if (hi > (int)map.layers.size()) hi = (int)map.layers.size();

    std::unordered_map<int, Texture> cache;
    Texture* s0 = load_slot(bundle_dir, cache, map.mc_slot0, map.var_slot0);
    Texture* s1 = load_slot(bundle_dir, cache, map.mc_slot1, map.var_slot1);

    auto resolve = [&](int high) -> Texture* {
        if (!s0 && !s1) return nullptr;
        if (high == 1 && s1) return s1;
        if (s0) return s0;
        return s1;
    };

    // Tile size from the first renderable cell's sheet (mirrors the toolkit).
    int TS = 0;
    for (const auto& layer : map.layers) {
        for (uint16_t word : layer) {
            Texture* ts = resolve(word >> 8);
            if (ts) { TS = (ts->w >= 512) ? 32 : 16; break; }
        }
        if (TS) break;
    }
    if (!TS) TS = 32;

    const int W = map.w * TS, H = map.h * TS;
    out.w = W; out.h = H;
    out.rgba.assign((size_t)W * H * 4, 0);
    if (opaque) for (size_t i = 0; i < (size_t)W * H; ++i) out.rgba[i * 4 + 3] = 255;  // opaque black

    for (int li = lo; li < hi; ++li) {
        const auto& layer = map.layers[li];
        const int ncell = (int)layer.size();
        for (int i = 0; i < ncell; ++i) {
            const uint16_t word = layer[i];
            const int tile = word & 0xff;
            const int high = word >> 8;
            if (tile == 0 && high == 0) continue;  // zero-skip
            Texture* ts = resolve(high);
            if (!ts) continue;
            const int cx = (i % map.w) * TS;
            const int cy = (i / map.w) * TS;
            const int tcols = ts->w / TS;
            if (tcols == 0) continue;
            const int tx = (tile % tcols) * TS;
            const int ty = (tile / tcols) * TS;
            if (tx + TS > ts->w || ty + TS > ts->h) continue;
            for (int yy = 0; yy < TS; ++yy) {
                const uint8_t* srow = &ts->rgba[((size_t)(ty + yy) * ts->w + tx) * 4];
                uint8_t* drow = &out.rgba[((size_t)(cy + yy) * W + cx) * 4];
                for (int xx = 0; xx < TS; ++xx) {
                    const uint8_t* s = srow + xx * 4;
                    uint8_t* d = drow + xx * 4;
                    const int sa = s[3];
                    if (sa == 255) {            // opaque src: PIL fast path -> copy
                        d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
                    } else if (sa == 0) {       // transparent src: keep dst
                        // no-op
                    } else if (d[3] == 255) {   // dst opaque -> straight over
                        d[0] = (uint8_t)div255(s[0] * sa + d[0] * (255 - sa));
                        d[1] = (uint8_t)div255(s[1] * sa + d[1] * (255 - sa));
                        d[2] = (uint8_t)div255(s[2] * sa + d[2] * (255 - sa));
                        d[3] = 255;
                    } else {                    // dst transparent/partial -> alpha-composite
                        int da = d[3], oa = sa + div255(da * (255 - sa));
                        if (oa > 0) {
                            d[0] = (uint8_t)((s[0] * sa + div255(d[0] * da * (255 - sa))) / oa);
                            d[1] = (uint8_t)((s[1] * sa + div255(d[1] * da * (255 - sa))) / oa);
                            d[2] = (uint8_t)((s[2] * sa + div255(d[2] * da * (255 - sa))) / oa);
                        }
                        d[3] = (uint8_t)oa;
                    }
                }
            }
        }
    }
    return out;
}

}  // namespace ffsmith
