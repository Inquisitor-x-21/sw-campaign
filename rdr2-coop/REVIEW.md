# Stage 1 review (before the first build on Windows)

Done in a Linux container without Windows, the game or the Script Hook SDK,
in passes: a first review, two re-checks (a fresh line-by-line read, then a
tool-driven pass), and a "smooth first try" pass. What could be checked here:

- Linux build with all tests: `core_tests` (18), `plugin_sim_tests`, the new
  `plugin_script_tests` (the main loop, see below) and `coopbot selftest`. All
  pass, also under AddressSanitizer + UndefinedBehaviorSanitizer, and the
  two-thread main-loop test under ThreadSanitizer.
- MinGW cross-compile of the plugin against `tools/ci/sdk_stub`: zero warnings,
  also with `-Wconversion -Wshadow` (to approximate MSVC `/W4`).
- The MinGW-built `core_tests.exe` and `coopbot.exe selftest` run under Wine:
  all pass, so ENet's Windows socket code works. A Windows `coopbot host`
  served three Linux bots.
- `build.bat` and `install.bat` run under Wine's `cmd`, from a folder named
  `rdr2-coop (1)`, with fake `vswhere`/`cmake` and a Steam-style game folder
  under `Program Files (x86)`. Checked: VS 2022 (generator by name), VS 2026
  (CMake's default), no Visual Studio, a failing test, first install,
  reinstall keeping the ini, missing `RDR2.exe`, no arguments. Wine can't
  judge two things: its `cmd` runs the right side of `||` even after success
  (a Wine bug), and it leaves `%cmdcmdline%` empty, so the double-click check
  is per Windows' documented behaviour only.
- The real plugin linked as `RDR2Coop.asi` (MinGW, against a stand-in
  `ScriptHookRDR2` import library) from a folder with spaces and parentheses.
  Under Wine, a small host program loaded it the way the game does, with a
  stand-in `ScriptHookRDR2.dll` running the script on a fiber like Script Hook
  and answering natives from the simulated game. It registered, ticked, drew
  its text, wrote its log next to the `.asi`, read the ini without warnings,
  never called a native off the script thread, and exited cleanly.
- `natives_gen.h` regenerated from today's alloc8or database: byte-identical,
  so every hash and signature is current.
- Line-by-line reads of `plugin/`, `core/`, `coopbot`, `build.bat`,
  `install.bat`.

`plugin/script.cpp` was the file without any test. `tests/script_test.cpp` now
runs the real `coopScriptMain` against the simulated game on a lockstep
"script thread", with real sessions on loopback as the other players. It covers
hotkeys, hosting, joining, stand-ins on foot and mounted, regroup, clock sync,
the panel, F6 reload, stats logging, leaving, a script restart and the log
fallback. Linux/macOS only, like `plugin_sim_tests`.

What could **not** be checked here: the real SDK `main.h` (dev-c.com is
blocked from this container), the MSVC build, and anything about how the game
reacts to a native. Those are in "Still to verify" below.

## Fixed

Each plugin fix has a regression test. Against the original plugin code, the
new tests fail 20 checks (14 in `plugin_sim_tests`, 6 in
`plugin_script_tests`); with the fixes, all pass.

1. **Stand-ins came back as the wrong model and horse after the game
   deleted them** (`game.cpp` `ensureModel`). A finished model request stayed
   "active" with its old start time. The next respawn (after mission cleanup,
   say) hit the 8 s timeout at once, if the game had unloaded the model in the
   meantime. The rider then came back as the fallback outlaw and the horse as
   the fallback breed. A finished request now ends, so each respawn starts
   fresh. The fake game now also unloads models nobody uses, which is how this
   showed up.
2. **Expired messages flickered back on screen** (`overlay.cpp`). Only expired
   messages at the front of the queue were removed. A short message behind a
   long one (such as "Bot1 joined" behind the 12 s "Hosting on port...") stayed,
   with a negative alpha that cycled through visible values. All expired
   messages are now removed.
3. **Crash or hang risk when quitting the game during a session**
   (`script.cpp`). The global state's destructors ran from `DllMain` at process
   exit. That deletes peds through natives from the wrong thread under the
   loader lock, and calls `WSACleanup`, which `DllMain` must not do. The state
   is now deliberately never destroyed. Others in the session see "lost
   connection" after the timeout, the same as for a crash.
4. **Jerky stand-in movement from a coarse clock** (`script.cpp` `nowMs`).
   `GetTickCount64` only advances every ~15.6 ms. That adds jitter to every
   state timestamp and makes interpolated positions repeat or jump between
   frames. It now uses `steady_clock` (QueryPerformanceCounter). coopbot and
   the tests already did.
5. **Stale handles after the game reloads** (`script.cpp`, `avatar.cpp`).
   Script Hook is expected to start `coopScriptMain` again after a reload,
   such as loading a save. Script Hook V does, and the original `initialized`
   flag suggests the author expected it too. The
   session lives on, but stand-in handles then pointed into the old world, or
   at whatever entity got that handle next. A restart is now logged, the
   handles are dropped without calling the game, and the relationship group is
   created again. Separately, a handle only counts as ours while it still has
   our model. A recycled handle is never moved, tasked or deleted.
6. **Panel showed "via host" for the host's own players** when the ping
   rounded to 0 ms (bots on the same PC). Ping is now shown for every direct
   link.
7. **No log at all when the game folder is read-only** (e.g. Rockstar
   Launcher or Epic installs under Program Files). Logs now fall back to
   `%LOCALAPPDATA%\RDR2Coop\logs`. The log and an on-screen message say where.
   (The second pass found that the first version of this decided "fallback
   used" by comparing path prefixes; it now just records which folder worked.)
8. **`build.bat` / `install.bat`:**
   - Visual Studio 2026, the current default download, wasn't supported (the
     generator was hard-coded to VS 2022). It now detects 2022 or 2026, and
     also finds the Build Tools edition.
   - It prefers Visual Studio's own CMake over any older one on PATH, which may
     not know the VS 2022 generator.
   - `vswhere` is no longer run through a quoted `Program Files (x86)` path
     inside `FOR /F`.
   - The SDK check now looks for the `.lib` too.
   - Both now end with `pause`, so a double-clicked window stays open and
     test results and errors can be read. Claude Code (which sets
     `CLAUDECODE`), CI, or a `nopause` argument skip it. (The first version
     tried to detect a double-click from `%cmdcmdline%` and wait with
     `timeout`; on the user's PC the window still closed at once.)
   - The copy-failed message now mentions administrator rights for Program
     Files.
   - Every variable the scripts set starts with `COOP_`. On the user's PC the
     first real run failed because `build.bat` kept its exit code in `RC`,
     which CMake reads as the path of the resource compiler ("Could not find
     the compiler specified in the environment variable RC: 1").
   - `.gitattributes` stores `.bat` files with CRLF exactly as they are. (The
     first version let git convert them, which made `git archive`, and so
     GitHub's "Download ZIP", hand out LF-only batch files. `cmd` can
     mis-jump labels in those.)
9. **Diagnostics for the two mount natives** (`avatar.cpp`). If
   `SET_PED_ONTO_MOUNT` or `CLEAR_PED_TASKS_IMMEDIATELY` hasn't taken effect
   after 60 frames, the log says so once. You can't see the screen, so that
   line is how you'll find out.
10. **A rejected blip style was retried every frame, silently** (`avatar.cpp`,
    found in the second pass). If `BLIP_ADD_FOR_ENTITY` returns 0 (say, the MP
    style doesn't exist in story mode), it was called again every frame with
    nothing in the log. It's now tried once per spawn or setting change, and
    the log says to try another `BlipStyle`.
11. **Disconnects while the game pauses scripts** (`script.cpp`, "first try"
    pass). ENet was only serviced from the script tick. If the pause menu, the
    map or a loading screen stops scripts for more than 3-8 s, everyone would
    time out. A background thread now services the session whenever the
    script hasn't ticked for 250 ms. It never calls natives, and it shares a
    mutex with the tick, so the two never touch the session at once. The log
    says how long scripts were paused (`the game didn't run scripts for N s`).
    The new test pauses the plugin for 9 s: with the thread the bot stays,
    without it the bot is dropped. ThreadSanitizer is clean.
12. **The `.asi` build failed in folders with parentheses** (`CMakeLists.txt`,
    found by the link test above). The post-build step that copies
    `RDR2Coop.ini` next to the `.asi` lacked `VERBATIM`, so paths like
    `rdr2-coop (1)` weren't escaped for the shell and the build stopped at
    the very end. Without `VERBATIM`, CMake gives no such protection on any
    platform, which also puts `C:\Users\First Last` paths at risk.
13. **Setup slips caught by the scripts.** `build.bat` now finds the SDK when
    the zip was extracted one folder too deep (`sdk\<zip name>\inc\main.h`)
    and says which folder it used. It always passes CMake an absolute SDK
    path. `install.bat` warns when `dinput8.dll`, Script Hook's `.asi`
    loader, is missing.

`core/src/session.cpp`: no bugs found. Join, leave, relay, reject, timeouts and
id reuse all hold up, and the loopback tests cover them well.

## Still to verify on the Windows PC (in this order)

1. **Pause menu, map and loading screens** (fixed in item 11, still worth
   seeing once). Host with bots, open the map for 15 s, then fast travel. The
   bots should stay, and the log shows how long each screen paused scripts.
2. **Real SDK, MSVC, and the batch files on real Windows.** `native_invoker.h` and `plugin.h` use only
   `scriptRegister`, `scriptUnregister(HMODULE)`, `scriptWait`, `nativeInit`,
   `nativePush64` and `nativeCall`. The SDK's `main.h` is expected to declare
   them exactly like the stub, plus a few unused extras. Build with
   `build.bat`, then fix any real `/W4` warnings. Its own `static` helpers in
   the SDK header may warn (C4505); that is harmless.
3. **After loading a save mid-session**, the log should show `script restarted`
   and the stand-ins should come back. If it doesn't appear, Script Hook RDR2
   doesn't restart scripts on reload, and stand-ins should still respawn
   through the "disappeared" path.
4. **Mounted Hybrid movement.** The rider gets `TASK_GO_STRAIGHT_TO_COORD` and
   the horse is expected to follow it. If horses slide or don't move: check for
   the new mount warnings, compare with `MountedMovement = Teleport`, and
   consider tasking the horse ped directly.
5. **Blip style.** `BLIP_STYLE_MP_PLAYER` may not exist in story mode. If no
   blip shows, look for `couldn't add a ... blip` in the log and try
   `BlipStyle = BLIP_STYLE_FRIENDLY` in the ini (F6 reloads).
6. **Outfit presets.** Check that `_EQUIP_META_PED_OUTFIT_PRESET` applies
   without an extra `_UPDATE_PED_VARIATION`. Outfit 0 and outfit 50 should look
   different.

## Open design points (not bugs, not changed)

- **Far-away players:** a stand-in spawned or moved beyond the streamed area
  has no collision and may fall, then warp back, every frame. A spawn radius
  (despawn past ~300 m, keep a coordinate blip) would avoid it.
- **Regroup and teleport-on-join across the map:** collision is requested once,
  right before the teleport, so a long jump may drop the player through the
  ground briefly.
- **Vehicles:** a player in a wagon, train or boat shows as an outlaw running
  alongside it at vehicle speed.
- **Horse coats:** breeds sync, coats don't (random per viewer).
- **"Friends join <address>":** shows the first IPv4 address, which can be a
  virtual adapter (Hyper-V, WSL). The log lists them all.
