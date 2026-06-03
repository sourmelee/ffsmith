#include "data/bundle.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

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

}  // namespace ffsmith
