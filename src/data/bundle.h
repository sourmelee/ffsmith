#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ffsmith {

struct Texture {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;  // w*h*4
    bool valid() const { return w > 0 && h > 0 && rgba.size() == (size_t)w * h * 4; }
};

struct FfMap {
    int w = 0, h = 0, n_layers = 0;
    int mc_slot0 = -1, var_slot0 = 0;
    int mc_slot1 = -1, var_slot1 = 0;
    std::vector<std::vector<uint16_t>> layers;  // [layer][cell]
    std::vector<uint8_t> event;                 // raw event-script region
    std::vector<uint8_t> pass;                  // per-cell 4-dir pass nibble (0 = solid); empty if none
    bool valid() const { return w > 0 && h > 0; }
};

Texture load_tex(const std::string& path);
FfMap   load_ffmap(const std::string& path);
bool    save_tex(const std::string& path, const Texture& t);

}  // namespace ffsmith
