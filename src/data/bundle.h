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
    int id = 0;                // event id (header u16; CallEvent 0x66 target)
    int x = 0, y = 0;          // tile position
    int w = 1, h = 1;          // trigger-rect size (CheckRangeEvent; boots 6/7/8)
    int type = 0;              // 0 = chara/NPC, 1 = trigger/auto (header[7])
    int boot = 0;              // boot/appear condition (header[8])
    int img = -1, var = 0;     // chara sprite id + variant
    std::vector<uint8_t> appear;   // 31-byte appear-condition block (header[9..0x27]; FFM3)
    std::vector<std::vector<uint8_t>> scripts;  // length-split bytecode blocks
};

struct FfMap {
    int w = 0, h = 0, n_layers = 0;
    int mc_slot0 = -1, var_slot0 = 0;
    int mc_slot1 = -1, var_slot1 = 0;
    int overhead_threshold = 0;   // layers with index > this are overhead (FieldClass+0xdc2c)
    int spawn_x = -1, spawn_y = -1, spawn_dir = 0;  // map default spawn (FFM4; +0xdc48..54)
    int field_bgm = 255;          // ReserveBGM id -> audio/snd0_{id}.wav (255 = none)
    int battle_bgm = 255;         // battle BGM id (255 = none)
    std::vector<std::vector<uint16_t>> layers;
    std::vector<uint8_t> event;     // raw event region (legacy; events[] is structured)
    std::vector<uint8_t> pass;      // per-cell 4-dir pass nibble (0 = solid)
    std::vector<Event> events;      // structured NPCs/triggers (FFM2)
    bool valid() const { return w > 0 && h > 0; }
};

Texture load_tex(const std::string& path);

struct ChipAnim { int type = 0, frames = 1, speed = 1; };  // animated chip (FieldClass::GetUpdateChipID)
std::unordered_map<int, std::unordered_map<int, ChipAnim>> load_chipanim(const std::string& path);
std::unordered_map<int, std::unordered_map<int, int>> load_chipfloor(const std::string& path);  // mc->inner->floorAttr (0x10=damage)
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

struct Item    { int id = 0; std::string name, desc; int atk = 0, def = 0, type = 0; };  // atk=weapon, def=armor, type=item_type category
struct CharRec { int id = 0; std::string name; int equip[6] = {0,0,0,0,0,0};
                 int job = 0, level = 1, str = 0, spd = 0, vit = 0, intl = 0, mnd = 0, hp = 0, mp = 0, chpk = 0; };
std::vector<Item>    load_items(const std::string& path);   // data/items.bin
std::vector<CharRec> load_chars(const std::string& path);   // data/chars.bin
struct Monster { int id = 0; std::string name; int hp = 0, atk = 0, def = 0, level = 1; long exp = 0, gil = 0; };
std::vector<Monster> load_monsters(const std::string& path);  // data/monsters.bin

struct LevelTable {                              // data/levels.bin (EXP thresholds + HP/MP growth)
    std::vector<uint32_t> thr;                   // thr[i] = cumulative EXP to reach level i+1
    std::vector<int> hp, mp;                     // per-level max HP / MP (index = level)
    bool valid() const { return !hp.empty(); }
    int levelFromExp(long e) const { int L = 0; for (uint32_t t : thr) { if (e >= (long)t) ++L; else break; } return L; }
    int maxHp(int L) const { if (hp.empty()) return 30; if (L < 0) L = 0; if (L >= (int)hp.size()) L = (int)hp.size()-1; return hp[L]; }
    int maxMp(int L) const { if (mp.empty()) return 0;  if (L < 0) L = 0; if (L >= (int)mp.size()) L = (int)mp.size()-1; return mp[L]; }
    long expForLevel(int L) const { return (L >= 1 && L-1 < (int)thr.size()) ? (long)thr[L-1] : 0; }
};
LevelTable load_levels(const std::string& path);

struct SpriteGeo { int isObject = 0, fx = 0, fy = 0, fw = 0, fh = 0, px = 0, py = 0; };  // field_anm per-sprite frame + anchor
std::unordered_map<int, SpriteGeo> load_spritegeo(const std::string& path);
struct Spell { int id = 0, type = 0, mp = 0, power = 0; std::string name; };  // type 0=dmg,1=heal
std::vector<Spell> load_spells(const std::string& path);  // data/spells.bin

// data/start.bin (FSTR): New Game start table from boot_data scenario section 1
// (GameClass::LoadScenarioData).  Record 0 = retail New Game start point.
struct StartInfo { int map = -1, x = 0, y = 0, story = 0; bool valid() const { return map >= 0; } };
std::vector<StartInfo> load_start(const std::string& path);

}  // namespace ffsmith
