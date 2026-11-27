# Project Status Report: *Through the Eyes of Carnage*

**Date:** 2026-11-27
**Repo:** github.com/Hardrivetech/ThroughTheEyesOfCaranage (branch `main`, HEAD `643286e`; everything is committed, and the last commit is not pushed)
**Studio:** Hardrive Technologies / Driveby Studios
**Genre:** High-octane first-person shooter in the *Hardcore Henry* style
**Stack:** Custom C++23 engine, OpenGL 4.6, GLFW 3.4, GLM 1.0.1, GLAD2. Vendored stb_image and miniaudio. Catch2 v3.7.1 for tests. CMake with Ninja.

## Overall Stage
**Vertical slice, pre-alpha (v0.1.0).** The engine, combat and level pipeline work end to end, and the game now has a flow: Level 01 (escape gauntlet) -> briefing -> Kodiak Island hub -> mission gates. The target is now a single contiguous Kodiak Island basin with modular points of interest. A 1 km x 1.4 km island skeleton with two POIs exists and now has a textured, weathered look (Phase 4); content is still thin and nothing has been playtested by a person yet.

## Health Check (verified 2026-11-27)
| Item | Result |
|---|---|
| Unit tests (Linux, Debug) | **362 test cases, all passing.** On a pristine copy of the commit: 100% pass in Debug and Release, stable in 3 random orders |
| Compiler warnings | **0** in project code (game and tests, Debug and Release, GCC on Linux) |
| Working tree | Clean. `saves/` (player saves) and `build-*/` are git-ignored |
| Game launched or played | **Not played.** Offscreen frames of the world, the combat HUD and both menus were rendered through the real renderer on the GPU (RX 6700 XT, Mesa, GL 4.6): shaders compile with no GL errors and the images were inspected. Input, audio, feel, and the live game loop (including the new save, Continue and pause-menu actions in `main.cpp`) are unverified; only their logic is unit-tested |
| Linux release binary | `build/Carnage` predates Phases 3 and 4 and the three milestones; rebuild before playing |
| Code size | About 14.0k lines in `src/` and 9.8k in `tests/`. Largest: `main.cpp` 1290, `Renderer.cpp` 856, `Level.cpp` 644 |
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
- **Kodiak Island hub** (`kodiak_hub.lvl`): one outdoor level with a landing pad, plaza, command post (door 0) and east yard (door 1). Doors open on approach, each zone has a checkpoint, and three animated gates are portals (Level 01, the practice range and the island). `kodiak_range.lvl` is a small practice level reachable through one gate.
- **Level network checker** (`LevelGraph`): verifies every portal, `next_level`, arrival spawn and briefing resolves, and that every level has a way back. It runs at startup and its problems are logged.
- **Broadphase:** `World` queries (rays, ground, ceiling, wall probes, collision) run through a uniform XZ grid, so they only examine nearby blocks. Results are bit-identical to the old full scan (checked against a verbatim copy of it, including tie-breaks), and doors need no grid updates. About 157x faster for rays and 387x for collision in a 3000-block, 2 km world.
- **AI sleep/wake and light culling:** enemies far from the player don't run their brain (vision raycast, movement, ground and wall queries) and stand still until the player comes within the wake radius (50 m; they sleep beyond 65 m, so there is no flicker on the boundary). Damage, a nearby hurt ally, a gunshot, a punch or a blast wakes them (and keeps them awake for 10 s). Sleeping enemies still count for objectives and can be shot. Room lights are culled by distance in one pass, and sounds beyond audible range skip their occlusion raycasts. `EnemyConfig` balance numbers are untouched; the radii live in a separate `ActivationConfig`. The F4 overlay and the `stats` command show awake/asleep enemies, lights submitted and world-query load.
- **Modular levels** (`include` record): a `.lvl` can include another (`include path=levels/pois/docks.lvl offset=120,0,-80 yaw=90 prefix=docks_`). Blocks, ramps, doors, lights, enemies, pickups, targets, triggers and named spawns are moved and rotated (quarter turns, integer-exact) into the parent. Cycles, depth beyond 8, fan-out beyond 256 and name collisions are reported as `file:line: message`, naming the included file. `LevelGraph` resolves spawns and portals in included files.
- **Open-world environment:** a per-level `environment visibility= far= shadow=` record sets fog density, camera far plane and sun-shadow radius. Defaults reproduce the old values exactly. The shadow box snaps to texels (no shimmer), the depth range and bias scale with the radius, and shadows fade out over the outer 10% of the box. The loader warns if fog does not hide the far plane.
- **Kodiak Island skeleton** (`kodiak_island.lvl`): 1 km x 1.4 km basin with flush roads, ridges, boulders, 21 lights, 17 enemies, and two included POIs (`pois/docks.lvl`, `pois/relay_tower.lvl`). Reached from Level 01 via `next_level=kodiak_island` (the briefing sits in between) and from the hub's third gate; it has a gate back to the hub.
- **Materials and shaders (Phase 4):**
  - Six texture-backed materials (`concrete_worn`, `metal_rusted`, `asphalt_wet`, `snow_packed`, `hazard_trim`, `grate_industrial`) plus an animated `portal`, chosen with `mat=<name>` on any block, ramp or door. Albedo, normal and roughness/metal/AO/glow textures are generated on the CPU at startup (`gfx/ProcTextures`, deterministic, tileable) and can be replaced from `assets/textures/`.
  - World-space triplanar sampling with a second, larger-scale sample blended in by noise, so big surfaces don't show a grid of repeats. `environment snow=` settles snow on upward-facing weathered surfaces. Hazard stripes carry a chase light; portals pulse and scroll.
  - Post pass: speed lines above sprint speed and a crimson vignette driven by camera trauma (`core/PostFx.hpp`). The ACES tone map and the distance haze were already in place.
- **Floor and depth fixes:** no two drawn top faces share a plane (roads sit 2 cm above the ground, overlapping footprints were carved apart), and `LevelAudit` (`findCoplanarTops`, `findSnowyInteriors`) fails the tests if one comes back. Interiors (hub command post, docks warehouse) have their own tile floors so snow does not settle indoors. The camera uses a reversed-Z projection (`perspectiveReverseZ`, `glClipControl` zero-to-one, `GL_GREATER`): at 1 km the old projection could not separate surfaces closer than 1.6 m, this separates 1 mm.
- **Saves** (`src/game/SaveSystem`): one `save` record in the `Record.hpp` format at `saves/slot<N>.sav` (slots 1-9): level, last checkpoint, health and armor, weapons and ammo, off hand, completed missions. Written automatically on a checkpoint, a portal, a level transition and a level completion, and by the pause menu or `save [slot]`; read by Continue or `load [slot]`. It never stores entity state, so a load rebuilds the level at the checkpoint. A missing, empty, oversized, binary or mangled file is rejected with a `[WRN]` (a fuzz test checks every truncation and byte change); out-of-range values are clamped and unknown weapons dropped. Completed missions persist across New Game and unlock Select Level / Hub.
- **Combat HUD** (`src/game/HudState` for the rules, `Hud.cpp` for drawing): a momentum gauge (full at twice sprint speed; its overdrive tier starts exactly where the PostFx speed lines do), `[E] SEIZE` for a staggered enemy, a human-shield integrity bar with the throw prompt, fading directional damage arcs around the crosshair, and below 25 HP a heartbeat vignette (`PostFx`) and pulsing crosshair brackets. Crimson / amber / steel palette with bracket corners.
- **Menus:** main (New Game, Continue with the saved level shown, Select Level / Hub, Settings, Quit) and pause (Resume, Load Last Checkpoint, Save Checkpoint, Restart Level, Settings, Quit to Main Menu). Mouse and keyboard (`Up`/`Down`/`W`/`S` + `Enter`/`Space`, `Esc` resumes); unavailable rows are dimmed and cannot be chosen.
- **Dev tools:** backtick console (`god`, `infammo`, `noclip`, `freeze`, `stats`, `spawn`, `kill_all`, `tp x y z` / `tp <spawn>`, `open_door`/`close_door`, `level <id> [spawn]`, `briefing <id>`, `save`/`load [slot]`, `progress`, `timescale`, `win`, and others), F3 dev menu, F4 profiler.
- **Tests:** 362 cases across `World`, `Movement`, `Combat`/`Weapons`, levels, prompts, briefings, the hub, broadphase, sleep/wake, includes, materials and procedural textures, post effects, the floor audit and depth, saves, the HUD state and the menus. Each phase's tests were mutation-checked by injecting bugs and confirming the suite caught them.

## Work Done in This Roadmap
| Commit | What |
|---|---|
| `a62c8ce` | Repo hygiene: line endings, untracked build artifacts, README rewrite |
| `3afa3ac` | Unit tests for World, Movement, Combat and Weapons |
| `23e6936` | Level data moved out of C++ into `level01_escape.lvl` and a loader |
| `2f71356` | Tutorial prompts and level-completion transitions |
| `f2b1946` | Fix: jumping under low ceilings shoved the player sideways |
| `68bd54b` | Skippable lore briefings |
| `e97f65b` | Kodiak hub, portals, named spawns, level network checker |
| `63c6952` | Phase 1: broadphase grid for `World` queries |
| `b1d2de7` | Phase 2: enemy sleep/wake and light culling |
| `5f673d4` | Phase 3: modular `.lvl` includes, environment record, shadow/fog tuning, Kodiak Island skeleton and POIs |
| `a8c6dd5` | Phase 4: procedural industrial materials, triplanar world shader, snow, animated portal/hazard, post effects, **fix: floors were lit from below** |
| `1a2ab61` | (another contributor) Windows cross-build, packaging and the briefing content |
| `a24deb6` | Fix z-fighting and interior snow; reverse-Z depth; level audit |
| `124d5b8` | Checkpoint and mission-progress saves (`SaveSystem`) |
| `dab7ada` | Restore the briefing header the rewrite dropped |
| `ed6bdb3` | (another contributor, pushed) Combat HUD state, low-health pulse, menu rows, `scripts/release.sh`; captured a snapshot of the milestone 3 work in progress |
| `643286e` | HUD and menu follow-up: fixes `throwReady` (a mutation-test value was committed in `ed6bdb3`), a menu refocus edge case, tests, README |

**Rendering bug found in Phase 4 and fixed:** `scene.frag` flipped the normal using `gl_FrontFacing`, but the level mesh and dynamic boxes are not wound consistently, so every top face of every block (all floors) was lit as if facing down. Every level, including Level 01, was far darker than intended. The shader now picks the side from screen-space derivatives. It was found by rendering real frames on the GPU; the unit tests cannot see it.

Phase 3 tests found two content bugs: a POI spawn 0.3 m above ground and roads raised 5 cm that put the default spawn inside a slab (roads are now flush).

Bugs found and fixed along the way: the ceiling shove (above), `World::setDoorOpen(-1)` disabling every ordinary block, the player starting at the controller's default position instead of the level spawn, and enter-triggered doors announcing "SECTOR CLEARED".

## Known Issues and Tech Debt
1. **Balance is untuned.** `EnemyConfig` in `src/game/Enemy.hpp` is labelled "all values are untuned guesses", and weapon, movement and throw numbers are unreviewed. Deliberately untouched until a manual playtest.
2. **Unverified in the live game.** The tutorial HUD, briefing backdrop and text layout, audio, portals, level switching, and the new save / Continue / pause-menu wiring in `main.cpp` compile and their logic is unit-tested, but none of it has been seen or heard in a running game. World rendering, the combat HUD and the menus were checked on offscreen frames only.
3. **Hub and range are first passes.** Layout, dimensions, lighting and pacing were authored from the level format alone.
4. **Briefing text and title are placeholders.** The four-beat briefing and its images are committed; its `title` is a stand-in that its author can change.
5. **`main.cpp` is large** (1164 lines) and holds the whole game loop, level switching and the console commands. It works, but the level and briefing flow would be easier to test if it moved out.
6. **Build needs the network.** CMake fetches dependencies through FetchContent, and GLAD generation needs Python with jinja2.
7. **Saves are checkpoint-only.** Loading rebuilds the level, so enemies, doors and cleared sectors start fresh (by design). The hub's mission gates are not visually locked or marked cleared; only Select Level / Hub is gated, and `completed_levels` is recorded for it. There is one autosave slot; slots 2-9 are console-only. The weapon list in a save is stored but every weapon is still always available (no unlock mechanic exists).
8. **The throw key is the grab key (`E`), not the left mouse button.** The shield HUD shows the key actually bound to Grab / Throw. Left mouse still fires the weapon while holding a shield.
9. **Depth precision is fixed for geometry that is not coplanar.** Reverse-Z removes the distance problem; two faces in exactly the same plane would still z-fight at any distance, which the level audit now prevents in the shipped levels.
10. **Look checked on screenshots only.** Phase 4 rendered frames of the island, hub and Level 01 through the real renderer on a GPU and inspected them, but nobody has played with it. Interiors (hub command post, Level 01) are still dark and heavily noisy; Level 01's legacy materials were not touched.
11. **The shader has no notion of a roof**, so snow settles on any upward-facing weathered surface in a level that sets `snow=`. The shipped levels avoid it by using tile for interiors, and a test (`findSnowyInteriors`) enforces that; new levels need to follow the same rule.
12. **Grating is opaque.** The holes are drawn dark but are not see-through (and are solid for collision).

Resolved earlier: line-ending churn, tracked build artifacts, out-of-date README, missing test coverage, hard-coded level data, the ceiling bug, and the compiler warning in the test suite.

## Not Yet Started or Missing
- More mission levels. Only Level 01 is a real mission; the hub's gates lead to it, the practice range and the open island (which has two POIs but no mission objective yet).
- The remaining Kodiak POIs (the island file reserves sites for a fuel depot, cannery, radar station and old bunker).
- Enemy variety beyond Melee and Ranged (no heavy, shielded, charger or boss).
- A boss encounter.
- Art: one humanoid mesh and one pillar prop. Audio: one music track; sound effects appear to be code-generated (not confirmed).
- Visible mission progress in the hub (gates marked cleared or locked), more save slots in the menu, and saving mid-level world state (deliberately out of scope).
- CI. Local build and release scripts exist (`scripts/build.sh` for Linux and a MinGW-w64 Windows build, `scripts/release.sh` to package and publish), but nothing runs automatically.

## Recommended Next Steps (priority order)
1. **Push the local commits** and do a manual playtest: walk Level 01, finish it, read the briefing, explore the hub and the island, use the gates and die in a few places. Log what feels wrong, and whether the island's scale and look hold up on a real screen.
2. **Tune balance** from that playtest (enemy damage and speed, throw damage, weapon feel). This is the first time the numbers can change.
3. **Refine the hub, range and island** (layout, scale, lighting), brighten the dark interiors (hub command post, Level 01), and commit the real briefing text and images.
4. **Content:** give the island a mission objective, fill its reserved POI sites, and add new enemy archetypes and a boss.
5. **Split up `main.cpp`** (level switching, briefing flow and console commands) to make that flow testable.
6. **CI:** a Linux and Windows build that runs the test suite, ideally with a headless render smoke test (the Phase 4 offscreen harness was a throwaway and is not in the repo).

## Risks
- **Feel and balance are unverified.** The systems are tested, but no human has played the full flow.
- **Thin content.** The hub offers three gates and only one is a real mission.
- **Windows and Linux divergence.** `.gitattributes` now normalizes line endings, but a MinGW-w64 cross-build and a Windows playtest zip were added by another contributor, but nothing here was built or run on Windows, and the new save, menu and HUD code has only been exercised on Linux.

## Questions for the Owner
- Can someone play through the current build and report back? That unblocks balance tuning and the hub layout.
- What are the target platforms (Windows only, or Linux too)? Should the Windows playtest zips stay in the repository?
- What is the priority for the next content: a second mission, new enemy types, or a boss? Is there a target date for a demo?
- Who writes the real briefing text and images?

---
*Status was derived from the git history, the source, and test runs (Debug and Release builds, 362 test cases, plus offscreen GPU renders). It was not derived from a gameplay session.*
