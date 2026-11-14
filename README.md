# Through the Eyes of Carnage

**Through the Eyes of Carnage** is a high-octane, hyper-kinetic first-person shooter built from scratch on a custom, zero-dependency C++23 game engine. Inspired by visceral industrial action aesthetics (*Hardcore Henry* style), the game features relentless combat, active melee lunges, hitscan ranged threats, multi-room arena progression, and a raw industrial cybergrind soundscape.

---

## 🚀 Key Architectural Features

Unlike projects tied to commercial engines, **Through the Eyes of Carnage** features a completely bespoke technical stack engineered for raw speed, low-level control, and deterministic performance:

* **Custom C++23 Engine:** Zero third-party game engine bloat. Built directly on OpenGL 4.6, GLFW, GLAD, and GLM.
* **Deterministic 60Hz Loop:** Fixed-timestep simulation ensuring rock-solid physics, consistent combat timing, and predictable state updates.
* **Optimized $O(1)$ Particle & Decal Systems:** High-performance memory pools for blood splatters, impact sparks, and world decals with automatic culling and ring-buffer management.
* **3D Spatialized Audio:** Custom 48-voice audio mixer featuring distance attenuation, velocity panning, and real-time audio positioning relative to the player camera.
* **Built-in Developer Suite:** Header-only unit-tested console command registry, live dev menu overlays, and an `F4` performance profiler tracking CPU splits, draw calls, and entity counts in real time.

---

## 🛠️ Project Structure

```text
src/
├── core/         # Fixed-timestep loop, spatial audio, and core math
├── dev/          # Header-only command console, DevState, and UI overlays
├── game/         # Player health, active enemy AI, inventory, and triggers
└── gfx/          # OpenGL renderer, O(1) particle system, and decal ring buffer
tests/            # Unit tests for console logic, tokenization, and command dispatch
CMakeLists.txt    # Modern CMake configuration
```

---

## 🕹️ Developer Console & Cheats

Press **``` (backtick)** during gameplay to open the developer console, **F3** to toggle the Dev Menu, and **F4** to display the real-time performance profiler.

**Supported Console Commands:**
* `god` — Toggle invulnerability
* `infammo` — Infinite weapon ammunition
* `noclip` — Fly freely through world geometry (`Space` = Ascend, `Ctrl` = Descend, `Shift` = Fast)
* `freeze` — Toggle enemy AI updates
* `stats` — Render telemetry and performance readouts
* `spawn <melee|ranged> [n]` — Spawn active combatants into the sector
* `kill_all` — Terminate all active enemies
* `tp x y z` — Teleport player coordinates
* `open_door <id>` / `close_door <id>` — Control level triggers and security gates
* `timescale <val>` — Scale simulation speed

---

## ⚙️ Building from Source

### Prerequisites
* A C++23 compatible compiler (MSVC 2022, GCC 13+, or Clang 16+)
* CMake (3.20 or higher)
* Ninja (recommended build tool)
* OpenGL 4.6 compatible GPU drivers

### Build Instructions (Windows / Linux)

```bash
# 1. Clone the repository
git clone https://github.com/Hardrivetech/ThroughTheEyesOfCaranage.git
cd ThroughTheEyesOfCaranage

# 2. Configure the project with CMake
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 3. Compile the engine
cmake --build build --config Release

# 4. Run the executable from your build output directory
./build/Carnage.exe
```

---

## 🎵 Soundtrack & Identity
The aggressive industrial death metal and cybergrind soundtrack is composed and produced by **Byt3TuneZ**, merging heavy algorithmic game mechanics with dark, futuristic hardware aesthetics.

---

## 📄 License
Developed independently by Hardrive Technologies / Driveby Studios ([hardrivetech.net](https://hardrivetech.net)). All rights reserved.