# Project Status Report: *Through the Eyes of Carnage*

**Date:** 2026-11-26
**Repo:** github.com/Hardrivetech/ThroughTheEyesOfCaranage (branch `main`, HEAD `268a3bc`)
**Studio:** Hardrive Technologies / Driveby Studios
**Genre:** High-octane first-person shooter in the *Hardcore Henry* style
**Stack:** Custom C++23 engine, OpenGL 4.6, GLFW 3.4, GLM 1.0.1, GLAD2. Vendored stb_image and miniaudio. Catch2 v3.7.1 for tests. CMake with Ninja.

## Overall Stage
**Vertical slice, pre-alpha (v0.1.0).** The core engine and gameplay loop work end to end. There is one playable level, built from hand-coded test geometry.

## Health Check (verified 2026-11-26)
| Item | Result |
|---|---|
| Unit tests (Linux, Debug) | **Pass: 24 test cases, 3316 assertions** (`ctest` 100%) |
| Compiler warnings | **0** in project code (game and tests, Linux, GCC) |
| Linux release binary | A `build/Carnage` binary from Nov 22 is present |
| Game launched or played during this review | **No.** There was no display in the analysis environment, so gameplay is unverified |
| Code size | About 10k lines in `src/` and `tests/` (largest files: `main.cpp` 978, `Renderer.cpp` 776, `Mesh.cpp` 372) |
| Open TODO/FIXME markers in code | None |

## Completed Features
- **Engine:**
  - Fixed 60 Hz timestep loop.
  - Custom global allocator.
  - Input system with rebindable keys, saved to an INI settings file with clamping.
  - Renderer with PBR-style materials, shadows, sky, post-processing and room lights.
  - O(1) particle pool and decal ring buffer.
- **Audio:** 3D spatial mixer with distance attenuation, panning and occlusion. Music is one track, *Steel and Sinew* by Byt3TuneZ.
- **Player:**
  - Movement with wall-run, ramps and ledges.
  - Camera feel (trauma shake, recoil).
  - Viewmodel and HUD.
  - Health with armor and i-frames, plus a death and checkpoint-respawn flow.
- **Combat:**
  - Hitscan and melee.
  - Weapons: Fists, Pistol, SMG, Shotgun, Rocket Launcher with explosions.
  - Throwable and melee pickups (pipe, bat, barrel).
- **Enemy AI:**
  - Idle, Patrol, Chase, Attack and Seized states.
  - Melee lunge and ranged hitscan archetypes, with line-of-sight, vision-cone and hearing checks.
  - **Latest addition (commit `1b09b0d`):** seize, grab, human shield and throw. A staggered enemy can be grabbed and used as a shield, or thrown to damage others.
- **Level flow:** 3 sectors (corridor, arena, pit) with 11 enemies. Clearing a sector opens a door or gate, clearing the pit completes the level, and checkpoint volumes are in place.
- **Menus and UI:** main menu, settings and text rendering.
- **Dev tools:** backtick console (`god`, `infammo`, `noclip`, `freeze`, `stats`, `spawn`, `kill_all`, `tp`, `open_door`/`close_door`, `timescale`, `win`), F3 dev menu, F4 profiler.

## Known Issues and Tech Debt
1. ~~Line-ending churn~~ **Fixed (Phase 1):** `.gitattributes` added (`* text=auto`, explicit LF for sources, docs and shaders) and the tree renormalized.
2. ~~Build artifacts tracked in git~~ **Fixed (Phase 1):** 83 generated files under `build-tests/` and `__cmake_systeminformation/` were untracked (kept on disk) and added to `.gitignore`.
3. ~~README out of date~~ **Fixed (Phase 1):** dependencies, feature set, controls and Linux and Windows build commands are now accurate.
4. **Balance is untuned:** `EnemyConfig` in `src/game/Enemy.hpp` is explicitly labelled "all values are untuned guesses".
5. **Test coverage gaps:** the tests cover console, settings, input, menu, AI, health, triggers, audio, particles, decals and seize. They do not cover `Movement`, weapon firing, `Combat` damage, explosions or `World` raycasts directly.
6. **Hard-coded content:** the level, targets, pickups and enemies are set up in code in `src/main.cpp`. There is no level data format.
7. **Build needs the network:** CMake fetches dependencies through FetchContent, and GLAD generation needs Python with jinja2.

## Not Yet Started or Missing
- A second level, or any content beyond the single test level.
- A level or data-driven content format.
- Enemy variety beyond Melee and Ranged (no heavy, shielded, charger or boss).
- Boss encounter. The only boss is a placeholder dummy at the end of the corridor.
- Art: only one humanoid mesh and one pillar prop.
- Audio: one music track. Sound effects appear to be code-generated (not confirmed).
- Gameplay balance and playtest pass.
- CI, packaging and a release build pipeline.

## Recommended Next Steps (priority order)
1. ~~Repo hygiene~~ Done (Phase 1).
2. **Playtest and tune:** a manual play session to tune enemy damage, speed, throw damage and weapon feel. Log findings.
3. **Add tests** for Movement, Combat and World raycasts before further gameplay changes.
4. **Content:** choose one of three directions: a data-driven level format, a second level, or new enemy archetypes and a boss.
5. **CI:** a Linux and Windows build that runs the test suite.

## Risks
- **Single hard-coded level:** content work will be slow until a level format exists.
- **Balance and feel unverified:** nothing has been playtested in this review.
- **Windows and Linux divergence:** the line-ending and tracked-build-artifact problems will keep causing noisy diffs and merge conflicts (a README merge conflict was already hit in commit `268a3bc`).

## Questions for the Owner
- What was the last thing being worked on? The recent commits only show seize, grab and throw.
- What are the target platforms (Windows only, or Linux too)?
- What are the priorities and the target date for a demo?

---
*Status was derived from the git history, the source, and a test run. It was not derived from a gameplay session.*
