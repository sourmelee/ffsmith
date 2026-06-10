#include "audio/audio.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace ffsmith {

AudioManager::~AudioManager() { shutdown(); }

static std::vector<uint8_t> read_all(const std::string& path) {
    std::vector<uint8_t> b;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return b;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        b.resize((size_t)n);
        if (std::fread(b.data(), 1, (size_t)n, f) != (size_t)n) b.clear();
    }
    std::fclose(f);
    return b;
}

bool AudioManager::init(const std::string& bundleDir) {
    dir_ = bundleDir;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "[audio] SDL audio init failed: %s\n", SDL_GetError());
        return false;
    }
    // data/audio.bin : "FAUD" + u16 loop_count + loop_count flag bytes
    auto ab = read_all(dir_ + "/data/audio.bin");
    if (ab.size() >= 6 && std::memcmp(ab.data(), "FAUD", 4) == 0) {
        int cnt = ab[4] | (ab[5] << 8);
        if ((size_t)(6 + cnt) <= ab.size())
            loopFlags_.assign(ab.begin() + 6, ab.begin() + 6 + cnt);
    }
    SDL_AudioSpec want{};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &AudioManager::mixCallback;
    want.userdata = this;
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have_, 0);  // 0 = force this format
    if (dev_ == 0) {
        std::fprintf(stderr, "[audio] open device failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(dev_, 0);  // begin playback
    return true;
}

void AudioManager::shutdown() {
    if (dev_) {
        SDL_CloseAudioDevice(dev_);
        dev_ = 0;
    }
}

bool AudioManager::loadWavConverted(const std::string& path, std::vector<uint8_t>& out) const {
    SDL_AudioSpec ws;
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if (!SDL_LoadWAV(path.c_str(), &ws, &buf, &len)) return false;   // decodes PCM/ADPCM WAV
    SDL_AudioCVT cvt;
    int r = SDL_BuildAudioCVT(&cvt, ws.format, ws.channels, ws.freq,
                              have_.format, have_.channels, have_.freq);
    if (r < 0) { SDL_FreeWAV(buf); return false; }
    if (r == 0) {                                   // already device format
        out.assign(buf, buf + len);
        SDL_FreeWAV(buf);
        return true;
    }
    cvt.len = (int)len;
    cvt.buf = (Uint8*)SDL_malloc((size_t)cvt.len * cvt.len_mult);
    if (!cvt.buf) { SDL_FreeWAV(buf); return false; }
    SDL_memcpy(cvt.buf, buf, len);
    SDL_ConvertAudio(&cvt);
    out.assign(cvt.buf, cvt.buf + cvt.len_cvt);
    SDL_free(cvt.buf);
    SDL_FreeWAV(buf);
    return true;
}

bool AudioManager::bgmLoops(int id) const {
    if (id <= 0) return true;                        // idx 0 always loops (engine quirk)
    if (id < (int)loopFlags_.size()) return loopFlags_[id] != 0;
    return true;                                     // default: loop
}

void AudioManager::playBgm(int id) {
    if (!dev_) return;
    if (id < 0 || id == 255) { stopBgm(); return; }
    if (id == curBgm_) return;
    std::vector<uint8_t> buf;
    char name[64];
    std::snprintf(name, sizeof name, "/audio/snd0_%d.wav", id);
    if (!loadWavConverted(dir_ + name, buf)) {
        std::fprintf(stderr, "[audio] BGM %d (%s) not loadable\n", id, name);
        return;
    }
    SDL_LockAudioDevice(dev_);
    bgm_ = std::move(buf);
    bgmPos_ = 0;
    bgmLoop_ = bgmLoops(id);
    curBgm_ = id;
    SDL_UnlockAudioDevice(dev_);
}

void AudioManager::stopBgm() {
    if (!dev_) return;
    SDL_LockAudioDevice(dev_);
    bgm_.clear();
    bgmPos_ = 0;
    curBgm_ = -1;
    SDL_UnlockAudioDevice(dev_);
}

void AudioManager::playSe(int id) {
    if (!dev_ || id < 0) return;
    auto it = seCache_.find(id);
    if (it == seCache_.end()) {
        std::vector<uint8_t> buf;
        char name[64];
        std::snprintf(name, sizeof name, "/audio/snd2_%d.wav", id);
        if (!loadWavConverted(dir_ + name, buf)) {
            seCache_[id] = {};                       // remember the miss
            return;
        }
        it = seCache_.emplace(id, std::move(buf)).first;
    }
    if (it->second.empty()) return;
    SDL_LockAudioDevice(dev_);
    if (voices_.size() < 16) voices_.push_back({ &it->second, 0 });
    SDL_UnlockAudioDevice(dev_);
}

void AudioManager::setBgmVolume(float v) {
    bgmGain_ = (int)(std::min(1.f, std::max(0.f, v)) * SDL_MIX_MAXVOLUME);
}
void AudioManager::setSeVolume(float v) {
    seGain_ = (int)(std::min(1.f, std::max(0.f, v)) * SDL_MIX_MAXVOLUME);
}

void SDLCALL AudioManager::mixCallback(void* user, Uint8* stream, int len) {
    static_cast<AudioManager*>(user)->mix(stream, len);
}

void AudioManager::mix(Uint8* stream, int len) {
    SDL_memset(stream, 0, (size_t)len);
    // BGM (loop the whole track on end, per flag)
    if (!bgm_.empty()) {
        int remaining = len;
        Uint8* out = stream;
        while (remaining > 0 && !bgm_.empty()) {
            size_t avail = bgm_.size() - bgmPos_;
            int chunk = (int)std::min((size_t)remaining, avail);
            SDL_MixAudioFormat(out, bgm_.data() + bgmPos_, have_.format, chunk, bgmGain_);
            bgmPos_ += chunk;
            out += chunk;
            remaining -= chunk;
            if (bgmPos_ >= bgm_.size()) {
                if (bgmLoop_) bgmPos_ = 0;
                else { bgm_.clear(); curBgm_ = -1; break; }
            }
        }
    }
    // One-shot SFX voices
    for (auto& v : voices_) {
        if (!v.buf) continue;
        size_t avail = v.buf->size() - v.pos;
        int chunk = (int)std::min((size_t)len, avail);
        SDL_MixAudioFormat(stream, v.buf->data() + v.pos, have_.format, chunk, seGain_);
        v.pos += chunk;
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
        [](const Voice& v) { return !v.buf || v.pos >= v.buf->size(); }),
        voices_.end());
}

} // namespace ffsmith
