#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace ffsmith {

struct Texture {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    bool valid() const { return w > 0 && h > 0 && rgba.size() == (size_t)w * h * 4; }
};

// A field event: NPC, object, or trigger (from the map chunk's event pack).
struct Event {
    int x = 0, y = 0;          // tile position
    int type = 0;              // 0 = chara/NPC, 1 = trigger/auto (header[7])
    int boot = 0;              // boot/appear condition (header[8])
    int img = -1, var = 0;     // chara sprite id + variant
    std::vector<std::vector<uint8_t>> scripts;  // length-split bytecode blocks
};

struct FfMap {
    int w = 0, h = 0, n_layers = 0;
    int mc_slot0 = -1, var_slot0 = 0;
    int mc_slot1 = -1, var_slot1 = 0;
    std::vector<std::vector<uint16_t>> layers;
    std::vector<uint8_t> event;     // raw event region (legacy; events[] is structured)
    std::vector<uint8_t> pass;      // per-cell 4-dir pass nibble (0 = solid)
    std::vector<Event> events;      // structured NPCs/triggers (FFM2)
    bool valid() const { return w > 0 && h > 0; }
};

Texture load_tex(const std::string& path);
FfMap   load_ffmap(const std::string& path);
bool    save_tex(const std::string& path, const Texture& t);
std::string find_map_key(const std::string& bundleDir, int mapId);  // resolve MapChange target

struct Font {                       // baked bitmap-font atlas (text/font.tex + .meta)
    Texture atlas;
    int cw = 0, ch = 0, cols = 0, first = 32;
    bool valid() const { return atlas.valid() && cw > 0 && cols > 0; }
};
std::unordered_map<int, std::string> load_messages(const std::string& path);  // text/messages.bin
Font load_font(const std::string& texPath, const std::string& metaPath);
std::vector<std::string> list_maps(const std::string& bundleDir);    // map keys, sorted by g/p/m
std::vector<int>         list_sprites(const std::string& bundleDir);  // unique fldchr ids, sorted

struct Item    { int id = 0; std::string name, desc; int atk = 0, def = 0; };  // atk=weapon, def=armor
struct CharRec { int id = 0; std::string name; int equip[6] = {0,0,0,0,0,0};
                 int job = 0, level = 1, str = 0, spd = 0, vit = 0, intl = 0, mnd = 0, hp = 0, mp = 0; };
std::vector<Item>    load_items(const std::string& path);   // data/items.bin
std::vector<CharRec> load_chars(const std::string& path);   // data/chars.bin
struct Monster { int id = 0; std::string name; int hp = 0, atk = 0, def = 0, level = 1; };
std::vector<Monster> load_monsters(const std::string& path);  // data/monsters.bin
struct Spell { int id = 0, type = 0, mp = 0, power = 0; std::string name; };  // type 0=dmg,1=heal
std::vector<Spell> load_spells(const std::string& path);  // data/spells.bin

}  // namespace ffsmith
