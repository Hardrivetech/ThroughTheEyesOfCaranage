# Through the Eyes of Carnage

**Through the Eyes of Carnage** is a high-octane, hyper-kinetic first-person action game in the *Hardcore Henry* style, built on a custom C++23 engine. It features momentum movement, dual-wield and melee combat, enemy seize / human-shield / throw mechanics, multi-sector level progression, and an industrial cybergrind soundtrack.

**Status:** pre-alpha vertical slice (v0.1.0). See [`STATUS.md`](STATUS.md) for the current project report.

---

## Key Architectural Features

* **Custom C++23 engine** on OpenGL 4.6, with a small set of focused libraries instead of a commercial engine (see [Dependencies](#dependencies)).
* **Deterministic 60 Hz loop:** fixed-timestep simulation for consistent physics, combat timing and state updates.
* **O(1) particle and decal systems:** fixed memory pools for blood, sparks and world decals, with automatic culling and ring-buffer management.
* **3D spatial audio:** a custom mixer with distance attenuation, panning and occlusion, relative to the player camera.
* **Developer suite:** a unit-tested console command registry, a live dev menu and an F4 profiler.

---

## Current Feature Set (Vertical Slice)

* **Momentum movement:** sprint, slide, jump / vault, wall-run, ramps and ledges.
* **Combat:**
  * Hitscan weapons: Pistol, SMG and Shotgun.
  * A Rocket Launcher with explosions.
  * Fists and picked-up melee items (pipe, bat, barrel).
  * **Dual-wield:** an optional left-hand weapon, with a modifier key for left-hand actions.
* **Enemies:** melee (lunge) and ranged (hitscan) AI with line-of-sight, vision-cone and hearing checks.
* **Seize / grab / human shield / throw:** a heavy melee hit staggers an enemy. A staggered enemy can be grabbed, used as a human shield that absorbs hitscan fire, or thrown to damage others.
* **3-sector level flow:** corridor, arena and pit. Clearing a sector opens its door or gate, clearing the pit completes the level, and checkpoints are placed along the way.
* **Player health:** health and armor with i-frames, plus a death and checkpoint-respawn flow.
* **Menus:** main menu and settings, including rebindable keys saved to `carnage_settings.ini`.

---

## Project Structure

```text
src/
├── core/         # Fixed-timestep loop, renderer, input, spatial audio, memory, logging
├── dev/          # Header-only command console, DevState and dev UI overlays
├── game/         # Movement, camera feel, weapons, combat, enemy AI, interaction, triggers, world, level loader
├── gfx/          # Meshes, shaders, humanoid model, particle system and decals
└── ui/           # Menus and text rendering
assets/           # Shaders, meshes, audio and levels (assets/levels/*.lvl)
third_party/      # Vendored single-header libraries (stb_image, miniaudio)
tests/            # Catch2 unit tests
CMakeLists.txt    # CMake configuration
```

---

## Level Files

Levels are plain-text `.lvl` files in `assets/levels/`, one record per line, so they can be edited without touching C++. The shipped level is `assets/levels/level01_escape.lvl` (three sectors: corridor, arena and pit).

```text
# record key=value key=value ...      (vectors are comma-separated, '#' starts a comment)
level   name=level01_escape
spawn   pos=0,0,10
block   min=-6,-75 max=6,15 top=0 material=tile color=0.38,0.38,0.36
block   min=10,-100 max=14,-84 h0=3.5 h1=0 axis=z          # a ramp
block   min=-3,-76 max=3,-75 top=4.8 visible=0 door=0      # a door
light   pos=0,4.4,-5 color=9,7.4,5.4 radius=16
target  pos=0,0,2 patrol=2
enemy   pos=0,0,-6 type=melee sector=0
pickup  kind=pipe pos=1.6,0.06,4 yaw=0.4
trigger when=sector_cleared sector=0 do=open_door door=0
trigger when=enter min=-16,-1,-92 max=16,6,-77 do=checkpoint point=0,0,-80
trigger when=sector_cleared sector=2 do=complete
```

The loader (`src/game/Level.hpp`) never aborts on a bad line: it skips the line, logs `file:line: message`, and keeps loading. It also rejects typo'd keys, and drops triggers that could never work (a door id that doesn't exist, or a sector with no enemies). See the header for the full record reference.

---

## Controls

Default bindings (all rebindable in the settings menu):

| Action | Key |
|---|---|
| Move | `W` `A` `S` `D` |
| Sprint | `Left Shift` |
| Crouch / slide | `C` or `Left Ctrl` |
| Jump / vault | `Space` |
| Fire right hand | Left mouse button |
| Fire left hand (when dual-wielding) | Right mouse button |
| Reload right / left | `R` / `Q` |
| Left-hand modifier | `Left Alt` |
| Weapons | `1` Fists, `2` Pistol, `3` SMG, `4` Shotgun, `5` Rocket Launcher |
| Grab / throw | `E` |
| Respawn | `T` |

Developer keys: `` ` `` (backtick) opens the console, `F3` toggles the dev menu and `F4` shows the performance profiler.

### Console commands

* `god`: toggle invulnerability
* `infammo`: infinite ammunition
* `noclip`: fly through geometry (`Space` ascend, `Ctrl` descend, `Shift` fast)
* `freeze`: toggle enemy AI updates
* `stats`: render telemetry and performance readouts
* `spawn <melee|ranged> [n]`: spawn enemies
* `kill_all`: kill all active enemies
* `tp x y z`: teleport the player
* `open_door <id>` / `close_door <id>`: control doors and gates
* `timescale <val>`: scale simulation speed
* `win`: trigger level completion

---

## Dependencies

| Dependency | Version | How it is obtained |
|---|---|---|
| C++23 compiler | GCC 13+, Clang 16+ or MSVC 2022 | system |
| CMake | 3.24+ | system |
| Ninja | any recent | system (recommended generator) |
| OpenGL | 4.6 capable GPU and drivers | system |
| Python 3 + `jinja2` | any recent | system; needed to generate the GLAD loader at configure time |
| GLFW | 3.4 | CMake `FetchContent` |
| GLM | 1.0.1 | CMake `FetchContent` |
| GLAD2 | v2.0.8 | CMake `FetchContent` |
| stb_image, miniaudio | vendored | `third_party/` |
| Catch2 | v3.7.1 | CMake `FetchContent`, only when tests are enabled |

`FetchContent` downloads GLFW, GLM, GLAD2 and (for tests) Catch2 on the first configure, so that step needs network access.

---

## Building from Source

### Linux

```bash
git clone https://github.com/Hardrivetech/ThroughTheEyesOfCaranage.git
cd ThroughTheEyesOfCaranage

# Configure and build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run (from the repo root, so carnage_settings.ini is written there)
./build/Carnage
```

### Windows

```bat
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\Carnage.exe
```

On Windows with MinGW the C++ runtime is linked statically, because the engine replaces global `operator new/delete` (`src/core/Memory.cpp`).

Assets are loaded from the source tree's `assets/` directory. Its path is baked into the binary at configure time (`CARNAGE_ASSET_DIR`).

### Running the tests

```bash
cmake -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCARNAGE_BUILD_TESTS=ON
cmake --build build-tests --target CarnageTests
ctest --test-dir build-tests --output-on-failure
```

---

## Soundtrack & Identity
The aggressive industrial death metal and cybergrind soundtrack is composed and produced by **Byt3TuneZ**.

---

## License
Developed independently by Hardrive Technologies / Driveby Studios ([hardrivetech.net](https://hardrivetech.net)). All rights reserved.
