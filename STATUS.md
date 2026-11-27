# Project Status Report: *Through the Eyes of Carnage*

**Date:** 2026-11-27
**Repo:** github.com/Hardrivetech/ThroughTheEyesOfCaranage (branch `main`, HEAD `e97f65b`, one commit ahead of `origin/main`)
**Studio:** Hardrive Technologies / Driveby Studios
**Genre:** High-octane first-person shooter in the *Hardcore Henry* style
**Stack:** Custom C++23 engine, OpenGL 4.6, GLFW 3.4, GLM 1.0.1, GLAD2. Vendored stb_image and miniaudio. Catch2 v3.7.1 for tests. CMake with Ninja.

## Overall Stage
**Vertical slice, pre-alpha (v0.1.0).** The engine, combat and level pipeline work end to end, and the game now has a flow: Level 01 (escape gauntlet) -> briefing -> Kodiak Island hub -> mission gates. Content is still thin (three levels) and nothing has been playtested by a person yet.

## Health Check (verified 2026-11-27)
| Item | Result |
|---|---|
| Unit tests (Linux, Debug) | **Pass: 154 test cases** (`ctest` 100%), stable in random order, no expected failures |
| Compiler warnings | **0** in project code (game and tests, Debug and Release, GCC on Linux) |
| Working tree | Clean. The last commit (`e97f65b`) is not pushed yet |
| Game launched or played | **No.** There was no display in the analysis environment, so rendering, audio, feel and the on-screen flow are unverified |
| Linux release binary | `build/Carnage` in the repo directory is from Nov 22 and is stale; rebuild before playing |
| Code size | About 11.2k lines in `src/` and 4.7k in `tests/`. Largest: `main.cpp` 1139, `Renderer.cpp` 840, `Level.cpp` 403, `Briefing.cpp` 346 |
| Open TODO/FIXME markers | None |

## Completed Features
- **Engine:** fixed 60 Hz timestep loop, custom global allocator, rebindable input saved to an INI settings file, PBR-style renderer (materials, shadows, sky, post-processing, room lights), O(1) particle pool and decal ring buffer.
- **Audio:** 3D spatial mixer with distance attenuation, panning and occlusion. One music track (*Steel and Sinew* by Byt3TuneZ), which also plays during briefings.
- **Player:** momentum movement (sprint, slide, jump and vault, wall-run, ramps, ledges), camera feel (trauma shake, recoil), viewmodel, HUD, health with armor and i-frames, death and checkpoint respawn.
- **Combat:** hitscan and melee, five weapons (Fists, Pistol, SMG, Shotgun, Rocket Launcher with explosions), dual-wield, throwable and swingable pickups (pipe, bat, explosive barrel).
- **Enemy AI:** melee (lunge) and ranged (hitscan) enemies with line-of-sight, vision cone and hearing, plus **seize -> human shield -> throw** on staggered enemies.
- **Data-driven levels** (`assets/levels/*.lvl`, plain text, shared parser in `src/core/Record.hpp`):
  - Records: blocks (flat and ramps), doors, lights, targets, enemies, pickups, triggers, spawns.
  - Triggers: open door, checkpoint, complete, tutorial prompt, portal.
  - The loader skips malformed lines with `file:line: message`, rejects typo'd keys, and drops triggers that could never work.
- **Tutorial prompts:** `do=prompt` triggers show a hint that fades on a timer or once the player does the taught action. `{action}` placeholders show the player's current keys. Level 01 has 11 prompts covering movement, melee, guns, explosions and seize/shield/throw.
- **Level transitions:** `next_level`, `next_spawn` and `briefing` on the level record. Levels are swapped in place, and a failed load falls back to the menu.
- **Briefings** (`assets/briefings/*.brf`): skippable lore screens with typewriter text, optional background image (plain fallback if missing), optional sound per beat. `SPACE`/`ENTER` advance, `ESC` skips. The console command `briefing <id>` previews one. The playback logic is a pure state machine that always terminates.
- **Kodiak Island hub** (`kodiak_hub.lvl`): one outdoor level with a landing pad, plaza, command post (door 0) and east yard (door 1). Doors open on approach, each zone has a checkpoint, and two glowing gates are portals. `kodiak_range.lvl` is a small practice level reachable through one gate.
- **Level network checker** (`LevelGraph`): verifies every portal, `next_level`, arrival spawn and briefing resolves, and that every level has a way back. It runs at startup and its problems are logged.
- **Broadphase:** `World` queries (rays, ground, ceiling, wall probes, collision) run through a uniform XZ grid, so they only examine nearby blocks. Results are bit-identical to the old full scan (checked against a verbatim copy of it, including tie-breaks), and doors need no grid updates. About 157x faster for rays and 387x for collision in a 3000-block, 2 km world.
- **Dev tools:** backtick console (`god`, `infammo`, `noclip`, `freeze`, `stats`, `spawn`, `kill_all`, `tp x y z` / `tp <spawn>`, `open_door`/`close_door`, `level <id> [spawn]`, `briefing <id>`, `timescale`, `win`, and others), F3 dev menu, F4 profiler.
- **Tests:** 142 cases across `World`, `Movement`, `Combat`/`Weapons`, levels, prompts, briefings and the hub. Each phase's tests were mutation-checked by injecting bugs and confirming the suite caught them.

## Work Done in This Roadmap
| Commit | What |
|---|---|
| `a62c8ce` | Repo hygiene: line endings, untracked build artifacts, README rewrite |
| `3afa3ac` | Unit tests for World, Movement, Combat and Weapons |
| `23e6936` | Level data moved out of C++ into `level01_escape.lvl` and a loader |
| `2f71356` | Tutorial prompts and level-completion transitions |
| `f2b1946` | Fix: jumping under low ceilings shoved the player sideways |
| `68bd54b` | Skippable lore briefings |
| `e97f65b` | Kodiak hub, portals, named spawns, level network checker (not pushed) |

Bugs found and fixed along the way: the ceiling shove (above), `World::setDoorOpen(-1)` disabling every ordinary block, the player starting at the controller's default position instead of the level spawn, and enter-triggered doors announcing "SECTOR CLEARED".

## Known Issues and Tech Debt
1. **Balance is untuned.** `EnemyConfig` in `src/game/Enemy.hpp` is labelled "all values are untuned guesses", and weapon, movement and throw numbers are unreviewed. Deliberately untouched until a manual playtest.
2. **Unverified in game.** The rendering and audio paths added in this roadmap (tutorial HUD, briefing backdrop and text layout, hub lighting and scale, portals and level switching in `main.cpp`) compile and their logic is unit-tested, but none of it has been seen or heard.
3. **Hub and range are first passes.** Layout, dimensions, lighting and pacing were authored from the level format alone.
4. **Placeholder lore.** The three beats in `level01_outro.brf` are generic filler, and no briefing images exist (the plain background is used).
5. **`main.cpp` is large** (1139 lines) and holds the whole game loop, level switching and the console commands. It works, but the level and briefing flow would be easier to test if it moved out.
6. **Build needs the network.** CMake fetches dependencies through FetchContent, and GLAD generation needs Python with jinja2.
7. **No progress persistence.** Completed missions are not remembered, so the hub has no locked or unlocked gates.

Resolved earlier: line-ending churn, tracked build artifacts, out-of-date README, missing test coverage, hard-coded level data, the ceiling bug, and the compiler warning in the test suite.

## Not Yet Started or Missing
- More mission levels. Only Level 01 is a real mission; the hub's second gate leads to the practice range.
- Enemy variety beyond Melee and Ranged (no heavy, shielded, charger or boss).
- A boss encounter.
- Art: one humanoid mesh and one pillar prop. Audio: one music track; sound effects appear to be code-generated (not confirmed).
- Save/load and mission progress.
- CI, packaging and a release build pipeline.

## Recommended Next Steps (priority order)
1. **Push `e97f65b`** and do a manual playtest: walk Level 01, finish it, read the briefing, explore the hub, use both gates and die in a few places. Log what feels wrong.
2. **Tune balance** from that playtest (enemy damage and speed, throw damage, weapon feel). This is the first time the numbers can change.
3. **Refine the hub and range** (layout, scale, lighting) and replace the placeholder briefing text and add images.
4. **Content:** a second mission level, new enemy archetypes and a boss.
5. **Split up `main.cpp`** (level switching, briefing flow and console commands) to make that flow testable.
6. **CI:** a Linux and Windows build that runs the test suite.

## Risks
- **Feel and balance are unverified.** The systems are tested, but no human has played the full flow.
- **Thin content.** The hub offers two gates and only one is a real mission.
- **Windows and Linux divergence.** `.gitattributes` now normalizes line endings, but the project has not been built on Windows in this roadmap (the Windows branch of `CMakeLists.txt` is untested).

## Questions for the Owner
- Can someone play through the current build and report back? That unblocks balance tuning and the hub layout.
- What are the target platforms (Windows only, or Linux too)?
- What is the priority for the next content: a second mission, new enemy types, or a boss? Is there a target date for a demo?
- Who writes the real briefing text and images?

---
*Status was derived from the git history, the source, and test runs (Debug and Release builds, 142 test cases). It was not derived from a gameplay session.*
