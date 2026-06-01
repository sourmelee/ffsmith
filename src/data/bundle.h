#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ffsmith {

// Raw RGBA8 image (straight alpha), as baked into a .tex (FTEX) file.
struct Texture {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;  // w*h*4
    bool valid() const { return w > 0 && h > 0 && rgba.size() == (size_t)w * h * 4; }
};

// A baked map (.ffmap / FFM0). Tile words: low byte = tile_num, high byte =
// slot selector (0 = slot0 tileset, 1 = slot1).
struct FfMap {
    int w = 0, h = 0, n_layers = 0;
    int mc_slot0 = -1, var_slot0 = 0;
    int mc_slot1 = -1, var_slot1 = 0;
    std::vector<std::vector<uint16_t>> layers;  // [layer][cell]
    std::vector<uint8_t> event;                 // raw event-script region
    bool valid() const { return w > 0 && h > 0; }
};

Texture load_tex(const std::string& path);     // invalid Texture on failure
FfMap   load_ffmap(const std::string& path);   // invalid FfMap on failure
bool    save_tex(const std::string& path, const Texture& t);

}  // namespace ffsmith
