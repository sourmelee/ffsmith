#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <SDL.h>

namespace ffsmith {

// Clean-room equivalent of the Android SoundCtrlClass (see [[ffd_android_audio]]):
// streams ONE looping BGM track and plays pooled one-shot SFX, mixed through a
// single SDL2 audio callback.  All audio is baked IMA-ADPCM WAV that SDL_LoadWAV
// decodes natively, so the engine needs no Ogg decoder and no extra dependency.
//
//   ReserveBGM(id) -> audio/snd0_{id}.wav  (looped per bgm_loop.dat flag)
//   ReserveSE(id)  -> audio/snd2_{id}.wav  (one-shot, cached)
class AudioManager {
public:
    AudioManager() = default;
    ~AudioManager();

    // Open the SDL audio device + load data/audio.bin (BGM loop flags). Returns
    // false if audio can't start (the engine then simply runs silent).
    bool init(const std::string& bundleDir);
    void shutdown();
    bool ok() const { return dev_ != 0; }

    void playBgm(int id);     // switch BGM (no-op if already playing id); 255/-1 = stop
    void stopBgm();
    int  currentBgm() const { return curBgm_; }
    void playSe(int id);      // fire a one-shot SFX

    void setBgmVolume(float v01);
    void setSeVolume(float v01);

private:
    static void SDLCALL mixCallback(void* user, Uint8* stream, int len);
    void mix(Uint8* stream, int len);
    bool loadWavConverted(const std::string& path, std::vector<uint8_t>& out) const;
    bool bgmLoops(int id) const;

    SDL_AudioDeviceID dev_ = 0;
    SDL_AudioSpec have_{};                 // actual device format
    std::string dir_;
    std::vector<uint8_t> loopFlags_;       // bgm_loop.dat: 1 = loop whole track

    // BGM — single streaming track, decoded to device format.
    std::vector<uint8_t> bgm_;
    size_t bgmPos_ = 0;
    bool   bgmLoop_ = true;
    int    curBgm_ = -1;
    int    bgmGain_ = SDL_MIX_MAXVOLUME;   // 0..128

    // SFX — cached PCM per id + a pool of active voices.
    std::unordered_map<int, std::vector<uint8_t>> seCache_;
    struct Voice { const std::vector<uint8_t>* buf; size_t pos; };
    std::vector<Voice> voices_;
    int    seGain_ = SDL_MIX_MAXVOLUME;
};

} // namespace ffsmith
