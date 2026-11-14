#pragma once

#include <glm/glm.hpp>

namespace carnage::audio {

// Every sound the game can trigger. Names (see Audio.cpp) double as the
// override filenames: assets/audio/<name>.wav replaces the synthesized sound.
enum class Sfx {
    Punch, Swing, Pistol, Smg, Shotgun, Rocket, DryFire, Reload, ReloadDone,
    Footstep, Land, Slide, Jump, Vault, WallRun,
    HitBody, HitHead, Kill, Explosion, Impact,
    Pickup, Throw,
    UiHover, UiClick,
    Hum,  // looping ambience, started by init()
    Count
};

// Subsystem hooks. init() succeeds even if no output device exists: the game
// then runs silently rather than refusing to start.
bool init();
void shutdown();

// Thread-safe for a single producer (the game thread). Never blocks or
// allocates. `pan` is -1 (left) .. +1 (right); `pitch` is a playback ratio.
void play(Sfx sfx, float gain = 1.0f, float pan = 0.0f, float pitch = 1.0f,
          float lpCoeff = 1.0f);

// 3D sounds. setListener() is called once per tick with the camera pose;
// playAt() derives distance attenuation and panning on the game thread and
// enqueues an ordinary voice, so the audio thread and 48-voice mixer are
// untouched. `occlusion` (0..1) lowers gain and applies a per-voice low-pass (muffling).
void setListener(const glm::vec3& pos, const glm::vec3& forward, const glm::vec3& right);
void playAt(Sfx sfx, const glm::vec3& pos, float gain = 1.0f, float pitch = 1.0f,
            float occlusion = 0.0f);

// Fades the main-menu track in (and the ambience out), or the reverse. The
// track restarts from the top each time it fades back in.
void setMenuMusic(bool on);

void  setMasterVolume(float v);  // 0..1
void  setMusicVolume(float v);   // 0..1, menu track only
void  setSfxVolume(float v);     // 0..1, effects and ambience
float masterVolume();

} // namespace carnage::audio
