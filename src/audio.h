#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "vendor/HandmadeMath.h"

// ============================================================
//  AudioSystem — SDL3 fire-and-forget sound playback
//
//  Place .wav files in assets/sounds/.
//  Missing files are silently skipped (placeholder-friendly).
//
//  Non-positional:  audio.play("name", volume)
//  Positional:      audio.play_3d("name", world_pos, ...)
// ============================================================

struct SoundBuffer {
    std::vector<float> samples;  // float32 stereo at device frequency
    float duration_ms = 0.0f;
};

struct ActiveVoice {
    SDL_AudioStream* stream    = nullptr;
    Uint64           expire_at = 0;      // SDL_GetTicks() deadline
};

struct AudioSystem {
    bool              enabled = false;
    SDL_AudioDeviceID device  = 0;
    SDL_AudioSpec     spec{};            // float32 stereo at device freq

    std::unordered_map<std::string, SoundBuffer> buffers;
    std::vector<ActiveVoice>                     active;

    // volume in [0,1]
    float master_volume = 1.0f;

    bool init();
    void shutdown();

    // Load a WAV from path, register under name. Returns false (silently) if file missing.
    bool load(const std::string& name, const std::string& path);

    // Non-positional: UI sounds, player weapon shots, etc.
    void play(const std::string& name, float volume = 1.0f);

    // Positional: distance attenuation (quadratic) + stereo pan.
    void play_3d(const std::string& name,
                 HMM_Vec3 source, HMM_Vec3 listener_pos,
                 HMM_Vec3 listener_right,
                 float base_volume = 1.0f,
                 float max_dist    = 30.0f);

    // Call once per frame to free finished voices.
    void update();

private:
    void emit(const SoundBuffer& buf, float gain_l, float gain_r);
};
