#include "data/bundle.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <algorithm>

namespace ffsmith {

static std::vector<uint8_t> read_file(const std::string& path) {
    std::vector<uint8_t> buf;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return buf;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        buf.resize((size_t)n);
        if (std::fread(buf.data(), 1, (size_t)n, f) != (size_t)n) buf.clear();
    }
    std::fclose(f);
    return buf;
}

static uint16_t rd_u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int16_t  rd_i16(const uint8_t* p) { return (int16_t)rd_u16(p); }
static uint32_t rd_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

Texture load_tex(const std::string& path) {
    Texture t;
    auto buf = read_file(path);
    if (buf.size() < 12 || std::memcmp(buf.data(), "FTEX", 4) != 0) return t;
    uint32_t w = rd_u32(&buf[4]);
    uint32_t h = rd_u32(&buf[8]);
    size_t need = 12 + (size_t)w * h * 4;
    if (buf.size() < need) return t;
    t.w = (int)w;
    t.h = (int)h;
    t.rgba.assign(buf.begin() + 12, buf.begin() + 12 + (size_t)w * h * 4);
    return t;
}

FfMap load_ffmap(const std::string& path) {
    FfMap m;
    auto buf = read_file(path);
    // Accept FFM0 (no collision) and FFM1 (with passability grid).
    if (buf.size() < 22 || std::memcmp(buf.data(), "FFM", 3) != 0) return m;
    const bool has_pass_block = (buf[3] >= '1');
    size_t o = 4;
    int w  = rd_u16(&buf[o]); o += 2;
    int h  = rd_u16(&buf[o]); o += 2;
    int nl = rd_u16(&buf[o]); o += 2;
    m.mc_slot0  = rd_i16(&buf[o]); o += 2;
    m.var_slot0 = rd_u16(&buf[o]); o += 2;
    m.mc_slot1  = rd_i16(&buf[o]); o += 2;
    m.var_slot1 = rd_u16(&buf[o]); o += 2;
    o += 4;  // reserved
    m.w = w; m.h = h; m.n_layers = nl;
    size_t cells = (size_t)w * h;
    for (int L = 0; L < nl; ++L) {
        if (o + cells * 2 > buf.size()) return m;
        std::vector<uint16_t> layer;
        layer.reserve(cells);
        for (size_t i = 0; i < cells; ++i) { layer.push_back(rd_u16(&buf[o])); o += 2; }
        m.layers.push_back(std::move(layer));
    }
    if (o + 4 <= buf.size()) {
        uint32_t elen = rd_u32(&buf[o]); o += 4;
        if (o + elen <= buf.size()) { m.event.assign(buf.begin() + o, buf.begin() + o + elen); o += elen; }
    }
    if (has_pass_block && o < buf.size()) {
        uint8_t has_pass = buf[o++];
        if (has_pass && o + cells <= buf.size()) {
            m.pass.assign(buf.begin() + o, buf.begin() + o + cells);
            o += cells;
        }
    }
    if (buf[3] >= '2' && o + 2 <= buf.size()) {  // FFM2 events block
        int ne = rd_u16(&buf[o]); o += 2;
        for (int e = 0; e < ne; ++e) {
            if (o + 9 > buf.size()) break;
            Event ev;
            ev.x = buf[o]; ev.y = buf[o+1]; ev.type = buf[o+2]; ev.boot = buf[o+3];
            ev.img = rd_i16(&buf[o+4]); ev.var = buf[o+6];
            int ns = rd_u16(&buf[o+7]); o += 9;
            for (int s = 0; s < ns; ++s) {
                if (o + 2 > buf.size()) break;
                int len = rd_u16(&buf[o]); o += 2;
                if (o + (size_t)len > buf.size()) break;
                ev.scripts.emplace_back(buf.begin() + o, buf.begin() + o + len);
                o += len;
            }
            m.events.push_back(std::move(ev));
        }
    }
    return m;
}

bool save_tex(const std::string& path, const Texture& t) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite("FTEX", 1, 4, f);
    uint8_t hdr[8] = {
        (uint8_t)t.w, (uint8_t)(t.w >> 8), (uint8_t)(t.w >> 16), (uint8_t)(t.w >> 24),
        (uint8_t)t.h, (uint8_t)(t.h >> 8), (uint8_t)(t.h >> 16), (uint8_t)(t.h >> 24),
    };
    std::fwrite(hdr, 1, 8, f);
    std::fwrite(t.rgba.data(), 1, t.rgba.size(), f);
    std::fclose(f);
    return true;
}

std::string find_map_key(const std::string& bundleDir, int mapId) {
    namespace fs = std::filesystem;
    const std::string want = "_m" + std::to_string(mapId) + ".ffmap";
    std::error_code ec;
    fs::path dir = fs::path(bundleDir) / "maps";
    if (fs::exists(dir, ec)) {
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            std::string fn = e.path().filename().string();
            if (fn.size() > want.size() &&
                fn.compare(fn.size() - want.size(), want.size(), want) == 0)
                return e.path().stem().string();   // "gG_pP_mMID"
        }
    }
    return "";
}

std::unordered_map<int, std::string> load_messages(const std::string& path) {
    std::unordered_map<int, std::string> out;
    auto buf = read_file(path);
    if (buf.size() < 8 || std::memcmp(buf.data(), "FMSG", 4) != 0) return out;
    uint32_t n = rd_u32(&buf[4]);
    size_t o = 8;
    for (uint32_t i = 0; i < n && o + 8 <= buf.size(); ++i) {
        uint32_t id = rd_u32(&buf[o]);
        uint32_t len = rd_u32(&buf[o + 4]);
        o += 8;
        if (o + len > buf.size()) break;
        out[(int)id] = std::string((const char*)&buf[o], len);
        o += len;
    }
    return out;
}

Font load_font(const std::string& texPath, const std::string& metaPath) {
    Font f;
    f.atlas = load_tex(texPath);
    auto m = read_file(metaPath);
    if (m.size() >= 14 && std::memcmp(m.data(), "FMET", 4) == 0) {
        f.cw = rd_u16(&m[4]); f.ch = rd_u16(&m[6]);
        f.cols = rd_u16(&m[8]); f.first = rd_u16(&m[10]);
    }
    return f;
}

std::vector<std::string> list_maps(const std::string& bundleDir) {
    namespace fs = std::filesystem;
    std::vector<std::string> out; std::error_code ec;
    fs::path dir = fs::path(bundleDir) / "maps";
    if (fs::exists(dir, ec))
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            std::string fn = e.path().filename().string();
            if (fn.size() > 6 && fn.compare(fn.size() - 6, 6, ".ffmap") == 0)
                out.push_back(e.path().stem().string());
        }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        int ga=0,pa=0,ma=0,gb=0,pb=0,mb=0;
        std::sscanf(a.c_str(), "g%d_p%d_m%d", &ga,&pa,&ma);
        std::sscanf(b.c_str(), "g%d_p%d_m%d", &gb,&pb,&mb);
        if (ga!=gb) return ga<gb;
        if (pa!=pb) return pa<pb;
        return ma<mb;
    });
    return out;
}

std::vector<int> list_sprites(const std::string& bundleDir) {
    namespace fs = std::filesystem;
    std::vector<int> out; std::error_code ec;
    fs::path dir = fs::path(bundleDir) / "sprites";
    if (fs::exists(dir, ec))
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            std::string fn = e.path().filename().string(); int img=0,var=0;
            if (std::sscanf(fn.c_str(), "fldchr%d_%d.tex", &img,&var) == 2 &&
                std::find(out.begin(), out.end(), img) == out.end())
                out.push_back(img);
        }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<Item> load_items(const std::string& path) {
    std::vector<Item> out; auto buf = read_file(path);
    if (buf.size() < 8 || std::memcmp(buf.data(), "FITM", 4) != 0) return out;
    uint32_t n = rd_u32(&buf[4]); size_t o = 8;
    for (uint32_t i = 0; i < n; ++i) {
        if (o + 6 > buf.size()) break;
        Item it; it.id = (int)rd_u32(&buf[o]); int nl = rd_u16(&buf[o + 4]); o += 6;
        if (o + (size_t)nl > buf.size()) break;
        it.name.assign((const char*)&buf[o], nl); o += nl;
        if (o + 2 > buf.size()) break;
        int dl = rd_u16(&buf[o]); o += 2;
        if (o + (size_t)dl > buf.size()) break;
        it.desc.assign((const char*)&buf[o], dl); o += dl;
        if (o + 4 <= buf.size()) { it.atk = rd_u16(&buf[o]); it.def = rd_u16(&buf[o + 2]); o += 4; }
        if (o + 1 <= buf.size()) { it.type = buf[o]; o += 1; }   // item_type category (0.7.13+)
        out.push_back(std::move(it));
    }
    return out;
}

std::vector<CharRec> load_chars(const std::string& path) {
    std::vector<CharRec> out; auto buf = read_file(path);
    if (buf.size() < 8 || std::memcmp(buf.data(), "FCHR", 4) != 0) return out;
    uint32_t n = rd_u32(&buf[4]); size_t o = 8;
    for (uint32_t i = 0; i < n; ++i) {
        if (o + 6 > buf.size()) break;
        CharRec c; c.id = (int)rd_u32(&buf[o]); int nl = rd_u16(&buf[o + 4]); o += 6;
        if (o + (size_t)nl > buf.size()) break;
        c.name.assign((const char*)&buf[o], nl); o += nl;
        if (o + 12 > buf.size()) break;
        for (int k = 0; k < 6; ++k) { c.equip[k] = rd_u16(&buf[o]); o += 2; }
        if (o + 12 <= buf.size()) {                 // appended: job,level (u8) + STR/SPD/VIT/INT/MND (u16)
            c.job = buf[o]; c.level = buf[o + 1]; o += 2;
            c.str = rd_u16(&buf[o]); c.spd = rd_u16(&buf[o + 2]); c.vit = rd_u16(&buf[o + 4]);
            c.intl = rd_u16(&buf[o + 6]); c.mnd = rd_u16(&buf[o + 8]); o += 10;
            if (o + 4 <= buf.size()) { c.hp = rd_u16(&buf[o]); c.mp = rd_u16(&buf[o + 2]); o += 4; }
        }
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<Monster> load_monsters(const std::string& path) {
    std::vector<Monster> out; auto buf = read_file(path);
    if (buf.size() < 8 || std::memcmp(buf.data(), "FMON", 4) != 0) return out;
    uint32_t n = rd_u32(&buf[4]); size_t o = 8;
    for (uint32_t i = 0; i < n; ++i) {
        if (o + 6 > buf.size()) break;
        Monster m; m.id = (int)rd_u32(&buf[o]); int nl = rd_u16(&buf[o + 4]); o += 6;
        if (o + (size_t)nl > buf.size()) break;
        m.name.assign((const char*)&buf[o], nl); o += nl;
        if (o + 7 > buf.size()) break;
        m.hp = rd_u16(&buf[o]); m.atk = rd_u16(&buf[o + 2]); m.def = rd_u16(&buf[o + 4]); m.level = buf[o + 6]; o += 7;
        out.push_back(std::move(m));
    }
    return out;
}

std::vector<Spell> load_spells(const std::string& path) {
    std::vector<Spell> out; auto buf = read_file(path);
    if (buf.size() < 8 || std::memcmp(buf.data(), "FSPL", 4) != 0) return out;
    uint32_t n = rd_u32(&buf[4]); size_t o = 8;
    for (uint32_t i = 0; i < n; ++i) {
        if (o + 9 > buf.size()) break;
        Spell sp; sp.id = rd_u16(&buf[o]); sp.type = buf[o + 2];
        sp.mp = rd_u16(&buf[o + 3]); sp.power = rd_u16(&buf[o + 5]);
        int nl = rd_u16(&buf[o + 7]); o += 9;
        if (o + (size_t)nl > buf.size()) break;
        sp.name.assign((const char*)&buf[o], nl); o += nl;
        out.push_back(std::move(sp));
    }
    return out;
}

std::unordered_map<int, std::unordered_map<int, ChipAnim>> load_chipanim(const std::string& path) {
    std::unordered_map<int, std::unordered_map<int, ChipAnim>> out;
    auto buf = read_file(path);
    if (buf.size() < 8 || std::memcmp(buf.data(), "FCAN", 4) != 0) return out;
    uint32_t nt = rd_u32(&buf[4]); size_t o = 8;
    for (uint32_t t = 0; t < nt && o + 4 <= buf.size(); ++t) {
        int mc = rd_u16(&buf[o]); int cnt = rd_u16(&buf[o + 2]); o += 4;
        auto& m = out[mc];
        for (int i = 0; i < cnt && o + 5 <= buf.size(); ++i) {
            int inner = rd_u16(&buf[o]);
            ChipAnim a; a.type = buf[o + 2]; a.frames = buf[o + 3]; a.speed = buf[o + 4]; o += 5;
            m[inner] = a;
        }
    }
    return out;
}

}  // namespace ffsmith
