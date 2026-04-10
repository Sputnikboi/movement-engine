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
//  Multiple variants per sound are supported automatically.
//  Name files:  footstep_1.wav, footstep_2.wav, footstep_3.wav
//  Plain name (footstep.wav) also works for single-variant sounds.
//  Each play() call picks a random variant, never the same one twice.
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

    // Each name maps to one or more variants; play() picks randomly.
    std::unordered_map<std::string, std::vector<SoundBuffer>> buffers;
    std::unordered_map<std::string, int>                      last_variant;
    std::vector<ActiveVoice>                                  active;

    float master_volume = 1.0f;

    bool init();
    void shutdown();

    // Load one WAV and append it as a variant under 'name'.
    // Call multiple times with the same name to register variants.
    bool load(const std::string& name, const std::string& path);

    // Load name_1.wav, name_2.wav, ... until one is missing,
    // then fall back to name.wav if no numbered variants were found.
    void load_variants(const std::string& name, const std::string& dir);

    // Non-positional: UI sounds, player weapon shots, etc.
    void play(const std::string& name, float volume = 1.0f);

    // Positional: quadratic distance falloff + equal-power stereo pan.
    void play_3d(const std::string& name,
                 HMM_Vec3 source, HMM_Vec3 listener_pos,
                 HMM_Vec3 listener_right,
                 float base_volume = 1.0f,
                 float max_dist    = 30.0f);

    // Call once per frame to free finished voices.
    void update();

private:
    const SoundBuffer* pick_variant(const std::string& name);
    void emit(const SoundBuffer& buf, float gain_l, float gain_r);
};
