# Stage 1 review (before the first build on Windows)

Done in a Linux container without Windows, the game or the Script Hook SDK.
What could be checked here:

- Linux build with all tests: `core_tests` (18), `plugin_sim_tests`,
  `coopbot selftest`. All pass, before and after the fixes below.
- MinGW cross-compile of the plugin against `tools/ci/sdk_stub`: zero warnings,
  also with `-Wconversion -Wshadow` (to approximate MSVC `/W4`).
- `natives_gen.h` regenerated from today's alloc8or database: byte-identical,
  so every hash and signature is current.
- Line-by-line read of `plugin/`, `core/`, `coopbot`, `build.bat`, `install.bat`.

What could **not** be checked here: the real SDK `main.h` (dev-c.com is
blocked from this container), the MSVC build, and anything about how the game
reacts to a native. Those are in "Still to verify" below.

## Fixed

Each plugin fix has a regression test in `tests/sim_test.cpp`. Against the
original plugin code, those tests fail 11 checks; with the fixes, all pass.

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
   Script Hook starts `coopScriptMain` again after a reload, such as loading a
   save; the original `initialized` flag suggests this was expected. The
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
8. **`build.bat` / `install.bat`:**
   - Visual Studio 2026, the current default download, wasn't supported (the
     generator was hard-coded to VS 2022). It now detects 2022 or 2026, and
     also finds the Build Tools edition.
   - It prefers Visual Studio's own CMake over any older one on PATH, which may
     not know the VS 2022 generator.
   - `vswhere` is no longer run through a quoted `Program Files (x86)` path
     inside `FOR /F`.
   - The SDK check now looks for the `.lib` too.
   - Started by double-click (or drag and drop onto `install.bat`), the window
     now stays open, so test results and errors can be read. It doesn't wait
     when input is redirected, so scripts and agents won't hang.
   - The copy-failed message now mentions administrator rights for Program
     Files.
   - `.gitattributes` keeps `.bat` files CRLF.
9. **Diagnostics for the two mount natives** (`avatar.cpp`). If
   `SET_PED_ONTO_MOUNT` or `CLEAR_PED_TASKS_IMMEDIATELY` hasn't taken effect
   after 60 frames, the log says so once. You can't see the screen, so that
   line is how you'll find out.

`core/src/session.cpp`: no bugs found. Join, leave, relay, reject, timeouts and
id reuse all hold up, and the loopback tests cover them well.

## Still to verify on the Windows PC (in this order)

1. **Does the session survive the pause menu, the map and loading screens?**
   ENet is only serviced from our script tick. If the game stops ticking
   scripts while paused (story mode pauses on the map and pause menu) or during
   fast travel or save loads, peers time out after 3-8 s
   (`peerTimeoutMin/MaxMs`). Test: host with bots, open the map for 15 s, then
   fast travel. If players drop, the fix is to service ENet from a background
   thread (with Session behind a mutex), or at least to raise the timeouts.
   This is the biggest open risk.
2. **Real SDK and MSVC.** `native_invoker.h` and `plugin.h` use only
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
   blip shows, try `BlipStyle = BLIP_STYLE_FRIENDLY` in the ini (F6 reloads).
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
