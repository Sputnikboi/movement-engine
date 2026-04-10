#include "audio.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <algorithm>

// ============================================================
//  Init / shutdown
// ============================================================

bool AudioSystem::init() {
    device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!device) {
        fprintf(stderr, "Audio: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_AudioSpec native{};
    SDL_GetAudioDeviceFormat(device, &native, nullptr);

    spec.format   = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq     = (native.freq > 0) ? native.freq : 44100;

    enabled = true;
    printf("Audio: opened device (freq=%d)\n", spec.freq);
    return true;
}

void AudioSystem::shutdown() {
    for (auto& v : active) {
        SDL_UnbindAudioStream(v.stream);
        SDL_DestroyAudioStream(v.stream);
    }
    active.clear();
    if (device) { SDL_CloseAudioDevice(device); device = 0; }
    enabled = false;
}

// ============================================================
//  Load
// ============================================================

bool AudioSystem::load(const std::string& name, const std::string& path) {
    if (!enabled) return false;

    SDL_AudioSpec wav_spec{};
    Uint8*        wav_data = nullptr;
    Uint32        wav_len  = 0;
    if (!SDL_LoadWAV(path.c_str(), &wav_spec, &wav_data, &wav_len)) {
        printf("Audio: skipping '%s' (%s)\n", path.c_str(), SDL_GetError());
        return false;
    }

    SDL_AudioStream* conv = SDL_CreateAudioStream(&wav_spec, &spec);
    if (!conv) {
        SDL_free(wav_data);
        fprintf(stderr, "Audio: conversion stream failed for '%s'\n", name.c_str());
        return false;
    }
    SDL_PutAudioStreamData(conv, wav_data, (int)wav_len);
    SDL_FlushAudioStream(conv);
    SDL_free(wav_data);

    int avail = SDL_GetAudioStreamAvailable(conv);
    SoundBuffer buf;
    if (avail > 0) {
        buf.samples.resize((size_t)avail / sizeof(float));
        SDL_GetAudioStreamData(conv, buf.samples.data(), avail);
    }
    SDL_DestroyAudioStream(conv);

    int frames    = (int)buf.samples.size() / 2;
    buf.duration_ms = spec.freq > 0 ? (frames / (float)spec.freq) * 1000.0f : 0.0f;

    buffers[name].push_back(std::move(buf));
    printf("Audio: loaded '%s' variant %d (%.0f ms)\n",
           name.c_str(), (int)buffers[name].size(), buffers[name].back().duration_ms);
    return true;
}

void AudioSystem::load_variants(const std::string& name, const std::string& dir) {
    // Try name_1.wav, name_2.wav, ... stop at first gap
    bool any = false;
    for (int i = 1; i <= 16; i++) {
        std::string path = dir + name + "_" + std::to_string(i) + ".wav";
        if (!load(name, path)) break;
        any = true;
    }
    // Fall back to plain name.wav if no numbered variants found
    if (!any)
        load(name, dir + name + ".wav");
}

// ============================================================
//  Variant picker — random, never repeats last pick
// ============================================================

const SoundBuffer* AudioSystem::pick_variant(const std::string& name) {
    auto it = buffers.find(name);
    if (it == buffers.end() || it->second.empty()) return nullptr;

    auto& variants = it->second;
    int n = (int)variants.size();
    if (n == 1) return &variants[0];

    int prev = last_variant.count(name) ? last_variant[name] : -1;
    int idx  = rand() % n;
    if (idx == prev) idx = (idx + 1) % n;
    last_variant[name] = idx;
    return &variants[idx];
}

// ============================================================
//  Internal emit — apply L/R gain and spawn a voice
// ============================================================

void AudioSystem::emit(const SoundBuffer& buf, float gain_l, float gain_r) {
    if (!enabled || buf.samples.empty()) return;

    constexpr int MAX_VOICES = 48;
    if ((int)active.size() >= MAX_VOICES) return;

    gain_l *= master_volume;
    gain_r *= master_volume;

    int n = (int)buf.samples.size();
    std::vector<float> out(n);
    for (int i = 0; i + 1 < n; i += 2) {
        out[i    ] = buf.samples[i    ] * gain_l;
        out[i + 1] = buf.samples[i + 1] * gain_r;
    }

    SDL_AudioStream* s = SDL_CreateAudioStream(&spec, &spec);
    if (!s) return;
    SDL_PutAudioStreamData(s, out.data(), (int)(n * sizeof(float)));
    SDL_FlushAudioStream(s);
    if (!SDL_BindAudioStream(device, s)) {
        SDL_DestroyAudioStream(s);
        return;
    }

    active.push_back({ s, SDL_GetTicks() + (Uint64)(buf.duration_ms * 1.1f) + 200 });
}

// ============================================================
//  Public play API
// ============================================================

void AudioSystem::play(const std::string& name, float volume) {
    const SoundBuffer* buf = pick_variant(name);
    if (!buf) return;
    emit(*buf, volume, volume);
}

void AudioSystem::play_3d(const std::string& name,
                          HMM_Vec3 source, HMM_Vec3 listener_pos,
                          HMM_Vec3 listener_right,
                          float base_volume, float max_dist) {
    const SoundBuffer* buf = pick_variant(name);
    if (!buf) return;

    HMM_Vec3 delta = HMM_SubV3(source, listener_pos);
    float dist = HMM_LenV3(delta);
    if (dist >= max_dist) return;

    float t   = 1.0f - (dist / max_dist);
    float vol = base_volume * t * t;
    if (vol < 0.002f) return;

    float pan = 0.0f;
    if (dist > 0.1f) {
        HMM_Vec3 dir = HMM_MulV3F(delta, 1.0f / dist);
        pan = std::clamp(HMM_DotV3(dir, listener_right), -1.0f, 1.0f);
    }

    float angle  = (pan + 1.0f) * 0.5f * (HMM_PI32 * 0.5f);
    float gain_l = vol * cosf(angle);
    float gain_r = vol * sinf(angle);
    emit(*buf, gain_l, gain_r);
}

// ============================================================
//  Per-frame cleanup
// ============================================================

void AudioSystem::update() {
    Uint64 now = SDL_GetTicks();
    active.erase(
        std::remove_if(active.begin(), active.end(), [now](ActiveVoice& v) {
            if (now >= v.expire_at) {
                SDL_UnbindAudioStream(v.stream);
                SDL_DestroyAudioStream(v.stream);
                return true;
            }
            return false;
        }),
        active.end()
    );
}
