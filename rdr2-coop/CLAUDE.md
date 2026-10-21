# RDR2 Co-op - briefing for Claude Code

Drop-in/drop-out co-op mod for Red Dead Redemption 2 **story mode** on PC.
Stage 1 (players, horses, time of day) is written and passes every test that
runs without the game. **It has never run inside RDR2.** You are picking it
up on the user's Windows PC to review it, build it, and run the in-game tests.

## Decisions already made with the user (don't change without asking)

- Story mode only. Never touch Red Dead Online, its netcode, or anti-cheat.
- Up to 4 players, host included. Drop-in/drop-out at any time.
- Only base-game content: free roam and the story. No custom missions.
- Host's save owns story progress; joiners ride along. Missions later run in
  "companion mode" (host runs them, others follow).
- Other players appear as generic outlaw NPC models with no story role
  (default `g_m_m_uniduster_01`), later RDO characters, then gang members.
- NPC sync (stage 2) starts at a 100 m radius and grows in steps, checking
  error and FPS in the logs at each step.
- Our own networking (ENet over UDP) on Script Hook RDR2 natives. RedM is a
  reference for knowledge only, never copied.
- No DRM or launcher bypasses; no cracked second copies.

## Review request (do this first)

Three review passes ran without Windows or the game; findings, fixes and what
is still unverified are in `REVIEW.md`. Read it first, then finish the parts
only this PC can check (items 2-4 below and the in-game tests). Highest-risk
areas, in order:

1. `plugin/script.cpp` - the main loop, hotkeys, session events, regroup,
   clock sync. Covered on Linux by `plugin_script_tests` against the
   simulated game; never yet run inside RDR2.
2. Native behaviour assumptions in `plugin/avatar.cpp` and `plugin/game.cpp`
   (signatures are verified, meanings are not):
   - `TASK_GO_STRAIGHT_TO_COORD` used to animate walking and riding; on a
     mounted rider it is expected to move the horse.
   - `SET_PED_ONTO_MOUNT(ped, horse, -1, TRUE)` seat index -1.
   - `CLEAR_PED_TASKS_IMMEDIATELY` expected to dismount instantly.
   - `CREATE_PED` + `_SET_RANDOM_OUTFIT_VARIATION` to make spawned peds visible.
   - `_BG_DISPLAY_TEXT` + `VAR_STRING(10, "LITERAL_STRING", ...)` for the overlay.
   - `BLIP_ADD_FOR_ENTITY(joaat("BLIP_STYLE_MP_PLAYER"), ...)`.
3. `plugin/native_invoker.h` + `plugin/plugin.h` against the **real** SDK
   `main.h`. Compile checks so far used the stand-in at
   `tools/ci/sdk_stub/inc/main.h`, which may differ.
4. MSVC build: only MinGW (`-Wall -Wextra`, zero warnings) has compiled this.
   Expect `/W4` warnings; fix real ones.
5. `core/src/session.cpp` - join/leave/relay logic (well tested, but review).
6. `build.bat` / `install.bat` robustness on real Windows paths.

## Layout

```
core/               portable: protocol, ENet session, interpolation, ini parser, log
plugin/             the .asi: game helpers, remote stand-ins, overlay, main loop
plugin/natives.list natives used -> tools/gen_natives.py -> plugin/natives_gen.h
tools/coopbot/      fake players for testing with one copy of the game
tests/              unit + 4-player loopback tests, simulated-game tests (tests/sim)
config/RDR2Coop.ini default settings (ships next to the .asi)
third_party/enet    ENet 1.3.18, MIT, vendored unmodified
sdk/                user puts the Script Hook RDR2 SDK here (not included)
```

## Rules for changes

- **Never type a native hash by hand.** Add the name to `plugin/natives.list`
  and run `python tools/gen_natives.py --fetch`. It pulls hashes, params and
  return types from alloc8or's RDR3 native database.
- Keep `core/` free of game and Windows-only code so tests and bots build anywhere.
- New settings go through `core/src/config.cpp` (with range checks + a test)
  and get documented in `config/RDR2Coop.ini`.
- Run `build.bat` after changes; it builds and runs `core_tests` and
  `coopbot selftest`. It skips its final `pause` under Claude Code (CLAUDECODE
  is set); elsewhere use `build.bat nopause`. (`plugin_sim_tests` and `plugin_script_tests` only
  build on Linux/macOS.)

## Setup on this PC (ask the user before installing anything)

1. Back up saves: `Documents\Rockstar Games\Red Dead Redemption 2\Profiles`.
2. Script Hook RDR2 matching the game version (ScriptHookRDR2.dll + dinput8.dll
   next to RDR2.exe) and its SDK in `sdk\` (`sdk\inc\main.h`,
   `sdk\lib\ScriptHookRDR2.lib`). Downloads: dev-c.com.
3. Visual Studio 2022 or 2026 with "Desktop development with C++".
4. `build.bat`, then `install.bat "<RDR2 folder>"`.

## In-game test loop

You can't see the game screen; the user reports what they see, and the logs
give the numbers. Order (from README "First test in game"):

1. `coopbot selftest` (no game).
2. User hosts (F7); `coopbot join --bots 1 --pattern still` - one outlaw should stand nearby.
   No text on screen? A new log file still means the plugin loaded.
3. `coopbot join --bots 3`, then `--mounted --speed 9`.
4. If movement looks wrong: `OnFootMovement` / `MountedMovement = Teleport`,
   user presses F6 (reload). In sync then = the movement tasks need tuning.
5. Map open for 15 s, then fast travel, with bots connected: they must stay.
   The log's `the game didn't run scripts for N s` lines show which screens
   pause scripts.
6. `--churn 10` for drop-in/drop-out, then a second instance or a friend.

Logs: `<game folder>\RDR2Coop_logs\*.log`, newest per launch (falls back to
`%LOCALAPPDATA%\RDR2Coop\logs` if the game folder is read-only). Every 10 s:
`sync | <player> | ping | error avg/max | extrapolated % | warps | buffer min`.
Healthy: error avg under ~1 m on foot, extrapolated under a few %, warps 0.
Tuning knobs live in `[Sync]` and `[Tuning]` of RDR2Coop.ini and reload live with F6.

## Known open items

- Horses spawn without saddles.
- Stand-ins may float or sink slightly on uneven ground (no ground snap).
- Two RDR2 instances on one PC may or may not launch; bots cover 4-player tests.
- Story missions may clean up nearby peds; stand-ins respawn and log it.
- Open question for the user: NPC sync (stage 2) before combat (stage 3), or swap?
