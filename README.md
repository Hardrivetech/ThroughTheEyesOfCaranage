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
assets/           # Shaders, meshes, audio, levels (assets/levels/*.lvl) and briefings (assets/briefings/*.brf)
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
spawn   name=from_level01_escape pos=0,0.2,40 yaw=0      # a named arrival point
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
trigger when=enter min=-2,-1,-57 max=2,4,-55 do=portal level=level01_escape   # a mission gate
trigger when=enter min=-4,-1,-32 max=4,4,-26 do=prompt time=10 clear_on=wall_run text="SPRINT + JUMP AT A WALL TO WALL-RUN"
```

**Tutorial prompts** (`do=prompt`) show a short hint when the player enters a volume. The text fades after `time` seconds, or sooner once the player does what it teaches (`clear_on=jump|slide|vault|wall_run|fire|grab|throw`). `{name}` placeholders show the player's current key for an action (`{grab}`, `{sprint}`, `{weapon1}`, `{lmb}`, ...). The loader checks the text, so a typo'd placeholder, a character the HUD font can't draw, or a line that is too long (90 characters once keys are filled in) is reported instead of showing garbage.

**Spawn points and portals:** a `spawn` without a name is the level's default start. Named spawns (`name=from_level01_escape`) are the places other levels send you to, and `yaw=` is the facing in degrees (0 faces north, -Z; 90 faces -X). A `do=portal level=<id> [spawn=<name>]` trigger loads another level when the player walks into its volume, arriving at that spawn. Entering a level always resets your checkpoint to the arrival point, so a checkpoint never carries over between levels.

**Level transitions:** put `next_level=<id>` (and optionally `next_spawn=<name>`) on the `level` record to continue into `assets/levels/<id>.lvl` when the level is completed (by clearing its `complete` sector or the `win` console command). Without it the game returns to the main menu. If the next level can't be loaded the game logs an error and falls back to the menu. Add `briefing=<id>` to play a briefing (below) first.

---

## Kodiak Island hub

`assets/levels/kodiak_hub.lvl` is the staging area between missions: one open outdoor level split into zones, with no streaming.

```text
landing pad (south) -> plaza -> command post (north, door 0)  -> gate: Level 01 - Escape
                          \-> east yard   (east,  door 1)  -> gate: Proving Ground (practice range)
```

* Doors open by themselves when you walk up to them (an `enter` trigger with `do=open_door`), from either side. Each zone has a checkpoint.
* The glowing gates are portals. `kodiak_range.lvl` is a small practice level (targets, enemies, pickups, cover) with a gate back to the hub.
* The flow: new game -> Level 01 -> its briefing -> the hub (arriving on the landing pad) -> pick a gate. Quitting to the main menu always makes "Play" start Level 01 again.
* At startup the game checks the whole network (every portal and `next_level` target exists, every arrival spawn exists, every briefing loads, every level has a way back) and logs anything broken.

---

## Briefings

A briefing is a skippable lore screen shown between levels: a few "beats", each with an optional background image and a few lines of text that type out on screen. Files live in `assets/briefings/<id>.brf`, in the same plain-text style as levels, so they can be edited without touching C++. The shipped example is `assets/briefings/level01_outro.brf` (placeholder lore, rewrite it freely).

```text
briefing name=level01_outro title="LEVEL 01 - ESCAPE" music=1
beat text="You made it out of the facility.\nNobody was supposed to." sfx=impact hold=3
beat text="Every door behind you is sealed." image=briefings/level01_a.png hold=4
```

* `text` is up to 360 characters and 8 explicit lines (`\n` is a line break); long lines word-wrap automatically. Only characters the HUD font can draw are allowed (letters, digits and `. - + : ! % / < > [ ] , _ = ( ) ' ? * ; | @ ~`).
* `image` is optional and relative to `assets/` (`.png`, `.jpg`, `.jpeg`, `.bmp`). A missing image shows a plain dark background and logs a warning.
* `hold` is how many seconds a beat stays after its text has finished appearing (default 4). `sfx` is `none`, `click`, `impact` or `explosion`. `music=1` plays the title-screen track while the briefing runs.
* **Controls:** `SPACE` / `ENTER` finish the text, then move to the next beat; `ESC` skips the whole briefing at any time. `SPACE` / `ENTER` are ignored for the first 0.3 s so keys mashed at the end of a fight don't skip it by accident.
* To preview one in game, open the console and run `briefing <id>`, then close the console.

Malformed lines are skipped and reported in the log (`file:line: message`), exactly like level files.

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
* `tp x y z` / `tp <spawn name>`: teleport the player to coordinates or to one of the level's spawns (`tp` with a bad name lists them)
* `open_door <id>` / `close_door <id>`: control doors and gates (an unknown id lists the level's doors)
* `timescale <val>`: scale simulation speed
* `win`: trigger level completion (Level 01 then plays its briefing and moves on to the hub)
* `level <id> [spawn]`: jump to a level, e.g. `level kodiak_hub`, `level kodiak_range from_kodiak_hub`
* `briefing <id>`: preview a briefing from `assets/briefings/`

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
