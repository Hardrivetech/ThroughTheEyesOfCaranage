#include "core/Audio.hpp"
#include "core/Log.hpp"
#include "core/Spatial.hpp"

#define MA_NO_ENCODING
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_GENERATION
#define MA_NO_FLAC
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace carnage::audio {

namespace {

constexpr std::uint32_t kRate      = 44100;
constexpr std::size_t   kSfxCount  = static_cast<std::size_t>(Sfx::Count);
constexpr std::size_t   kVoices    = 48;
constexpr std::size_t   kQueueSize = 256;  // power of two
constexpr float         kTwoPi     = glm::two_pi<float>();

constexpr std::array<const char*, kSfxCount> kNames{
    "punch", "swing", "pistol", "smg", "shotgun", "rocket", "dryfire", "reload",
    "reload_done", "footstep", "land", "slide", "jump", "vault", "wallrun",
    "hit_body", "hit_head", "kill", "explosion", "impact", "pickup", "throw",
    "ui_hover", "ui_click", "hum"};

struct Cmd {
    Sfx   sfx   = Sfx::Count;
    float gain  = 1.0f;
    float pan   = 0.0f;
    float pitch = 1.0f;
    float lpCoeff = 1.0f;  // one-pole low-pass coefficient; >= 1 means bypass
};

struct Voice {
    const std::vector<float>* data = nullptr;  // immutable after init
    double pos   = 0.0;
    float  gainL = 0.0f, gainR = 0.0f, pitch = 1.0f;
    bool   loop  = false;
    float  lpCoeff = 1.0f, lpState = 0.0f;
};

struct State {
    ma_device device{};
    bool      deviceOpen = false;

    std::array<std::vector<float>, kSfxCount> bank;  // mono float PCM
    std::array<Voice, kVoices> voices{};  // audio thread only

    std::array<Cmd, kQueueSize> queue{};  // SPSC ring
    std::atomic<std::uint32_t>  head{0};  // written by producer
    std::atomic<std::uint32_t>  tail{0};  // written by consumer

    std::atomic<float> master{0.8f};
    std::atomic<float> musicVol{1.0f};
    std::atomic<float> sfxVol{1.0f};

    // Main-menu music: interleaved stereo s16, decoded once at init.
    std::vector<std::int16_t> music;
    std::size_t               musicFrame = 0;     // audio thread only
    float                     musicGain  = 0.0f;  // audio thread only
    float                     ambGain    = 1.0f;  // audio thread only
    std::atomic<bool>         menuMusic{false};
    Listener                  listener{};  // game thread only
};

State g;

// --- synthesis -------------------------------------------------------------

struct Noise {
    std::uint32_t s = 0x1234567u;
    float next() {  // -1..1
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<std::int32_t>(s)) * (1.0f / 2147483648.0f);
    }
};

float expEnv(float t, float k) { return std::exp(-t * k); }

template <class F>
std::vector<float> make(float seconds, F&& f) {
    const auto n = static_cast<std::size_t>(seconds * static_cast<float>(kRate));
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kRate);
        out[i] = f(t, seconds);
    }
    // Short fade-out removes end clicks.
    const std::size_t fade = std::min<std::size_t>(n, 128);
    for (std::size_t i = 0; i < fade; ++i)
        out[n - 1 - i] *= static_cast<float>(i) / static_cast<float>(fade);
    return out;
}

// Gunshot: pitched-down thump + filtered noise crack, saturated for punch.
std::vector<float> gun(float dur, float thumpHz, float thumpK, float noiseK, float lp, float drive,
                       std::uint32_t seed) {
    Noise n{seed};
    float phase = 0.0f, low = 0.0f;
    return make(dur, [&](float t, float) {
        const float hz = thumpHz * (1.0f + 2.5f * expEnv(t, 35.0f));
        phase += kTwoPi * hz / static_cast<float>(kRate);
        low += (n.next() - low) * lp;
        const float body  = std::sin(phase) * expEnv(t, thumpK);
        const float crack = (n.next() * 0.35f + low) * expEnv(t, noiseK);
        return std::tanh((body * 1.1f + crack) * drive);
    });
}

std::vector<float> blip(float dur, float hz, float k, float noiseMix = 0.0f, std::uint32_t seed = 7) {
    Noise n{seed};
    return make(dur, [&](float t, float) {
        return (std::sin(kTwoPi * hz * t) * (1.0f - noiseMix) + n.next() * noiseMix) * expEnv(t, k);
    });
}

void synthesize() {
    auto B = [](Sfx s) -> std::vector<float>& { return g.bank[static_cast<std::size_t>(s)]; };

    B(Sfx::Pistol)  = gun(0.45f, 110.0f, 16.0f, 22.0f, 0.45f, 2.2f, 11);
    B(Sfx::Smg)     = gun(0.22f, 140.0f, 30.0f, 38.0f, 0.55f, 2.0f, 12);
    B(Sfx::Shotgun) = gun(0.80f,  65.0f, 9.0f, 11.0f, 0.30f, 2.8f, 13);

    {   // Rocket: launch whoosh, hissing noise rising then decaying.
        Noise n{14}; float low = 0.0f;
        B(Sfx::Rocket) = make(0.9f, [&](float t, float d) {
            low += (n.next() - low) * 0.25f;
            const float env = std::min(t * 30.0f, 1.0f) * expEnv(t, 3.5f);
            return std::tanh((low * 1.4f + std::sin(kTwoPi * 55.0f * t) * 0.6f) * env * 1.6f) *
                   (1.0f - t / d);
        });
    }
    {   // Explosion: sub rumble + brown noise whose cutoff falls over time.
        Noise n{15}; float low = 0.0f;
        B(Sfx::Explosion) = make(1.9f, [&](float t, float) {
            const float cut = 0.02f + 0.5f * expEnv(t, 3.0f);
            low += (n.next() - low) * cut;
            const float rumble = std::sin(kTwoPi * (38.0f + 30.0f * expEnv(t, 2.5f)) * t);
            return std::tanh((low * 3.0f * expEnv(t, 1.6f) + rumble * 0.9f * expEnv(t, 2.0f)) * 1.8f);
        });
    }
    {   // Punch: dull thud with a little skin slap.
        Noise n{16}; float low = 0.0f;
        B(Sfx::Punch) = make(0.22f, [&](float t, float) {
            low += (n.next() - low) * 0.3f;
            return std::tanh((std::sin(kTwoPi * (95.0f - 40.0f * t) * t) * expEnv(t, 24.0f) +
                              low * expEnv(t, 55.0f)) * 1.8f);
        });
    }
    {   // Swing: band-passed noise swelling in and out.
        Noise n{17}; float a = 0.0f, b = 0.0f;
        B(Sfx::Swing) = make(0.28f, [&](float t, float d) {
            const float x = n.next();
            a += (x - a) * 0.12f;
            b += (a - b) * 0.12f;
            const float s = std::sin(glm::pi<float>() * t / d);
            return (a - b) * 6.0f * s * s;
        });
    }
    {   // Footstep: low thump + filtered scuff.
        Noise n{18}; float low = 0.0f;
        B(Sfx::Footstep) = make(0.14f, [&](float t, float) {
            low += (n.next() - low) * 0.12f;
            return (std::sin(kTwoPi * 72.0f * t) * 0.7f + low * 1.5f) * expEnv(t, 38.0f);
        });
    }
    {   // Land: heavier, longer.
        Noise n{19}; float low = 0.0f;
        B(Sfx::Land) = make(0.30f, [&](float t, float) {
            low += (n.next() - low) * 0.09f;
            return std::tanh((std::sin(kTwoPi * 55.0f * t) * 1.0f + low * 1.6f) * expEnv(t, 16.0f) * 1.4f);
        });
    }
    {   // Slide: sustained high-passed hiss.
        Noise n{20}; float low = 0.0f;
        B(Sfx::Slide) = make(0.55f, [&](float t, float d) {
            const float x = n.next();
            low += (x - low) * 0.5f;
            const float s = std::min(t * 20.0f, 1.0f) * std::min((d - t) * 8.0f, 1.0f);
            return (x - low) * 0.5f * s;
        });
    }
    {   // Jump / vault / wall-run: short airy puffs of different weight.
        auto puff = [](float dur, float coef, float k, float gain, std::uint32_t seed) {
            Noise n{seed}; float low = 0.0f;
            return make(dur, [&, coef, k, gain](float t, float) {
                low += (n.next() - low) * coef;
                return low * gain * std::min(t * 60.0f, 1.0f) * expEnv(t, k);
            });
        };
        B(Sfx::Jump)    = puff(0.16f, 0.20f, 18.0f, 1.6f, 21);
        B(Sfx::Vault)   = puff(0.30f, 0.14f, 9.0f, 2.0f, 22);
        B(Sfx::WallRun) = puff(0.22f, 0.35f, 12.0f, 1.2f, 23);
    }
    B(Sfx::DryFire)    = blip(0.06f, 1400.0f, 70.0f, 0.35f, 24);
    B(Sfx::Impact)     = blip(0.10f, 300.0f, 45.0f, 0.55f, 25);
    B(Sfx::HitBody)    = blip(0.12f, 180.0f, 36.0f, 0.45f, 26);
    B(Sfx::HitHead)    = blip(0.16f, 1250.0f, 28.0f, 0.15f, 27);
    B(Sfx::Throw)      = B(Sfx::Swing);
    B(Sfx::UiHover)    = blip(0.035f, 1300.0f, 90.0f);
    B(Sfx::UiClick)    = blip(0.07f, 820.0f, 55.0f, 0.1f, 28);
    {
        auto two = [](float f0, float f1, float gap, float dur) {
            return make(dur, [=](float t, float) {
                const float a = std::sin(kTwoPi * f0 * t) * expEnv(t, 40.0f);
                const float u = t - gap;
                const float b = u > 0.0f ? std::sin(kTwoPi * f1 * u) * expEnv(u, 40.0f) : 0.0f;
                return (a + b) * 0.6f;
            });
        };
        B(Sfx::Pickup) = two(520.0f, 780.0f, 0.06f, 0.2f);
        B(Sfx::Kill)   = two(130.0f, 90.0f, 0.07f, 0.4f);
    }
    {   // Reload: two metallic clicks (mag out, mag in).
        Noise n{29};
        B(Sfx::Reload) = make(0.35f, [&](float t, float) {
            auto click = [&](float u, float hz) {
                return u > 0.0f ? (std::sin(kTwoPi * hz * u) * 0.6f + n.next() * 0.4f) * expEnv(u, 90.0f) : 0.0f;
            };
            return click(t, 2100.0f) + click(t - 0.17f, 1500.0f);
        });
        B(Sfx::ReloadDone) = make(0.14f, [&](float t, float) {
            return (std::sin(kTwoPi * 700.0f * t) * 0.5f + n.next() * 0.5f) * expEnv(t, 60.0f);
        });
    }
    {   // Ambience: a dark, decaying building. 10 s loop; tones are integer-Hz so
        // they loop exactly, the noise layer is cross-faded at the seam.
        constexpr float kLoop = 10.0f, kXfade = 1.0f;
        const auto n  = static_cast<std::size_t>(kLoop * kRate);
        const auto xf = static_cast<std::size_t>(kXfade * kRate);
        std::vector<float> raw(n + xf);

        Noise nz{31};
        float w1 = 0.0f, w2 = 0.0f, cph = 0.0f, c1 = 0.0f;

        auto drip = [](float u) {  // water drop: falling chirp, fast decay
            if (u < 0.0f) return 0.0f;
            const float ph = kTwoPi * (1100.0f * u + 15.0f * (1.0f - std::exp(-60.0f * u)));
            return std::sin(ph) * expEnv(u, 45.0f);
        };

        for (std::size_t i = 0; i < raw.size(); ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kRate);

            // Wind through broken windows: heavily low-passed noise with slow gusts.
            w1 += (nz.next() - w1) * 0.015f;
            w2 += (w1 - w2) * 0.015f;
            const float gust = 0.6f + 0.4f * std::sin(kTwoPi * 0.2f * t + 1.0f);
            const float wind = w2 * 12.0f * gust;

            // Sub-bass room tone with a slow swell (27/41/56 Hz, all periodic in 10 s).
            const float swell  = 0.8f + 0.2f * std::sin(kTwoPi * 0.1f * t);
            const float rumble = (std::sin(kTwoPi * 27.0f * t) * 0.45f + std::sin(kTwoPi * 41.0f * t) * 0.30f +
                                  std::sin(kTwoPi * 56.0f * t) * 0.12f) * swell;

            // Distant drips with echoing tails, kept clear of the loop seam.
            const float dr = (drip(t - 2.3f) + 0.35f * drip(t - 2.49f) + 0.15f * drip(t - 2.71f) +
                              drip(t - 5.1f) * 0.8f + 0.28f * drip(t - 5.31f) +
                              drip(t - 7.9f) * 0.9f + 0.30f * drip(t - 8.12f) + 0.12f * drip(t - 8.35f)) * 0.18f;

            // Groaning metal: low saw with wobble, softened by a one-pole filter.
            float creak = 0.0f;
            const float u = t - 3.6f;
            if (u > 0.0f && u < 1.0f) {
                const float f = 95.0f + 12.0f * u + 9.0f * std::sin(kTwoPi * 3.0f * u);
                cph += kTwoPi * f / static_cast<float>(kRate);
                if (cph > kTwoPi) cph -= kTwoPi;
                const float saw = cph / glm::pi<float>() - 1.0f;
                c1 += (saw - c1) * 0.05f;
                const float e = std::sin(glm::pi<float>() * u);
                creak = c1 * e * e * 0.5f;
            }
            raw[i] = wind * 0.5f + rumble * 0.35f + dr + creak;
        }

        auto& h = B(Sfx::Hum);
        h.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            if (i >= xf) { h[i] = raw[i]; continue; }
            const float a = glm::half_pi<float>() * static_cast<float>(i) / static_cast<float>(xf);
            h[i] = raw[i] * std::sin(a) + raw[n + i] * std::cos(a);  // equal-power seam
        }
    }
}

// Decodes the main-menu track (assets/audio/menu_music.* or the first known
// filename) into stereo s16 at the device rate. Missing file = no music.
void loadMusic() {
    for (const char* name : {"menu_music.mp3", "menu_music.wav", "Steel_and_Sinew.mp3"}) {
        const std::string path = std::string(CARNAGE_ASSET_DIR) + "/audio/" + name;
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 2, kRate);
        ma_decoder dec;
        if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS) continue;

        constexpr ma_uint64 kChunk = 16384;
        std::vector<std::int16_t> pcm;
        std::vector<std::int16_t> chunk(kChunk * 2);
        for (;;) {
            ma_uint64 got = 0;
            ma_decoder_read_pcm_frames(&dec, chunk.data(), kChunk, &got);
            if (got == 0) break;
            pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got * 2));
        }
        ma_decoder_uninit(&dec);
        if (pcm.empty()) continue;
        g.music = std::move(pcm);
        log::info("Menu music: {} ({:.1f} s)", name,
                  static_cast<double>(g.music.size() / 2) / static_cast<double>(kRate));
        return;
    }
    log::info("No menu music found in assets/audio");
}

// Replaces a synthesized sound with assets/audio/<name>.wav when it exists.
void loadOverrides() {
    for (std::size_t i = 0; i < kSfxCount; ++i) {
        const std::string path = std::string(CARNAGE_ASSET_DIR) + "/audio/" + kNames[i] + ".wav";
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, kRate);
        ma_decoder dec;
        if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS) continue;
        ma_uint64 frames = 0;
        if (ma_decoder_get_length_in_pcm_frames(&dec, &frames) == MA_SUCCESS && frames > 0) {
            std::vector<float> pcm(static_cast<std::size_t>(frames));
            ma_uint64 read = 0;
            ma_decoder_read_pcm_frames(&dec, pcm.data(), frames, &read);
            pcm.resize(static_cast<std::size_t>(read));
            g.bank[i] = std::move(pcm);
            log::info("Audio override: {}.wav", kNames[i]);
        }
        ma_decoder_uninit(&dec);
    }
}

// --- mixer (audio thread) --------------------------------------------------

void startVoice(const Cmd& c) {
    if (c.sfx >= Sfx::Count) return;
    const auto& data = g.bank[static_cast<std::size_t>(c.sfx)];
    if (data.empty()) return;

    Voice* slot = nullptr;
    float  best = -1.0f;
    for (Voice& v : g.voices) {
        if (!v.data) { slot = &v; break; }
        const float progress = static_cast<float>(v.pos / static_cast<double>(v.data->size()));
        if (!v.loop && progress > best) { best = progress; slot = &v; }  // steal the oldest
    }
    if (!slot) return;

    const float p = (std::clamp(c.pan, -1.0f, 1.0f) + 1.0f) * glm::quarter_pi<float>();
    slot->data  = &data;
    slot->pos   = 0.0;
    slot->pitch = std::max(c.pitch, 0.05f);
    slot->gainL = std::cos(p) * c.gain;
    slot->gainR = std::sin(p) * c.gain;
    slot->loop  = c.sfx == Sfx::Hum;
    slot->lpCoeff = slot->loop ? 1.0f : c.lpCoeff;
    slot->lpState = 0.0f;
}

void dataCallback(ma_device*, void* out, const void*, ma_uint32 frames) {
    float* o = static_cast<float*>(out);
    std::fill(o, o + static_cast<std::size_t>(frames) * 2, 0.0f);

    std::uint32_t t = g.tail.load(std::memory_order_relaxed);
    const std::uint32_t h = g.head.load(std::memory_order_acquire);
    while (t != h) { startVoice(g.queue[t & (kQueueSize - 1)]); ++t; }
    g.tail.store(t, std::memory_order_release);

    const float master = g.master.load(std::memory_order_relaxed);
    const float sfxVol = g.sfxVol.load(std::memory_order_relaxed);
    const float musVol = g.musicVol.load(std::memory_order_relaxed);

    // Menu music crossfades with the ambience (~0.8 s).
    const bool  wantMusic = g.menuMusic.load(std::memory_order_relaxed) && !g.music.empty();
    const float step      = 1.0f / (0.8f * static_cast<float>(kRate));
    const float blockStep = step * static_cast<float>(frames);
    const float ambTarget = wantMusic ? 0.0f : 1.0f;
    g.ambGain += std::clamp(ambTarget - g.ambGain, -blockStep, blockStep);

    for (Voice& v : g.voices) {
        if (!v.data) continue;
        const float  vg = (v.loop ? g.ambGain : 1.0f) * sfxVol;
        const auto&  d = *v.data;
        const double n = static_cast<double>(d.size());
        for (ma_uint32 i = 0; i < frames; ++i) {
            if (v.pos >= n) {
                if (v.loop) v.pos -= n; else { v.data = nullptr; break; }
            }
            const auto  i0 = static_cast<std::size_t>(v.pos);
            const auto  i1 = (i0 + 1 < d.size()) ? i0 + 1 : (v.loop ? 0 : i0);
            const float f  = static_cast<float>(v.pos - static_cast<double>(i0));
            float s  = d[i0] + (d[i1] - d[i0]) * f;
            if (v.lpCoeff < 0.999f) { v.lpState += v.lpCoeff * (s - v.lpState); s = v.lpState; }
            o[2 * i]     += s * v.gainL * vg;
            o[2 * i + 1] += s * v.gainR * vg;
            v.pos += static_cast<double>(v.pitch);
        }
    }

    if (wantMusic || g.musicGain > 0.0f) {
        constexpr float kMusicVol = 0.7f / 32768.0f;
        const std::size_t total = g.music.size() / 2;
        if (!wantMusic && g.musicGain <= 0.0f) g.musicFrame = 0;
        for (ma_uint32 i = 0; i < frames && total > 0; ++i) {
            g.musicGain += std::clamp((wantMusic ? 1.0f : 0.0f) - g.musicGain, -step, step);
            if (g.musicFrame >= total) g.musicFrame = 0;  // loop
            const float k = g.musicGain * kMusicVol * musVol;
            o[2 * i]     += static_cast<float>(g.music[2 * g.musicFrame]) * k;
            o[2 * i + 1] += static_cast<float>(g.music[2 * g.musicFrame + 1]) * k;
            ++g.musicFrame;
        }
        if (!wantMusic && g.musicGain <= 0.0f) g.musicFrame = 0;
    }
    for (ma_uint32 i = 0; i < frames * 2; ++i) o[i] = std::tanh(o[i] * master);
}

} // namespace

bool init() {
    synthesize();
    loadOverrides();
    loadMusic();

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate        = kRate;
    cfg.dataCallback      = dataCallback;
    cfg.performanceProfile = ma_performance_profile_low_latency;

    if (ma_device_init(nullptr, &cfg, &g.device) != MA_SUCCESS) {
        log::warn("Audio: no playback device, running silent");
        return true;
    }
    if (ma_device_start(&g.device) != MA_SUCCESS) {
        log::warn("Audio: device failed to start, running silent");
        ma_device_uninit(&g.device);
        return true;
    }
    g.deviceOpen = true;
    log::info("Audio: {} ({} Hz out)", g.device.playback.name, g.device.sampleRate);
    play(Sfx::Hum, 0.6f);
    return true;
}

void shutdown() {
    if (g.deviceOpen) {
        ma_device_uninit(&g.device);  // stops and joins the audio thread
        g.deviceOpen = false;
    }
    for (auto& b : g.bank) { b.clear(); b.shrink_to_fit(); }
    g.music.clear();
    g.music.shrink_to_fit();
}

void play(Sfx sfx, float gain, float pan, float pitch, float lpCoeff) {
    if (!g.deviceOpen) return;
    const std::uint32_t h = g.head.load(std::memory_order_relaxed);
    const std::uint32_t t = g.tail.load(std::memory_order_acquire);
    if (h - t >= kQueueSize) return;  // full: drop rather than block
    g.queue[h & (kQueueSize - 1)] = {sfx, gain, pan, pitch, lpCoeff};
    g.head.store(h + 1, std::memory_order_release);
}

void setMenuMusic(bool on) { g.menuMusic.store(on, std::memory_order_relaxed); }

void setListener(const glm::vec3& pos, const glm::vec3& forward, const glm::vec3& right) {
    g.listener = {pos, forward, right};  // game thread only
}

void playAt(Sfx sfx, const glm::vec3& pos, float gain, float pitch, float occlusion) {
    const SpatialResult r = spatialize(g.listener, pos);
    const float         v = gain * r.gain * occlusionGain(occlusion);
    if (v < 0.01f) return;  // inaudible: don't burn a voice
    const float dist = glm::length(pos - g.listener.pos);
    play(sfx, v, r.pan, pitch, lowpassCoeff(lowpassCutoff(dist, occlusion), static_cast<float>(kRate)));
}

void setMusicVolume(float v) { g.musicVol.store(std::clamp(v, 0.0f, 1.0f), std::memory_order_relaxed); }
void setSfxVolume(float v)   { g.sfxVol.store(std::clamp(v, 0.0f, 1.0f), std::memory_order_relaxed); }

void setMasterVolume(float v) { g.master.store(std::clamp(v, 0.0f, 1.0f) * 1.25f, std::memory_order_relaxed); }
float masterVolume() { return g.master.load(std::memory_order_relaxed) / 1.25f; }

} // namespace carnage::audio
