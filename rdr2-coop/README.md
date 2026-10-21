# RDR2 Co-op (stage 1)

Drop-in/drop-out co-op for **Red Dead Redemption 2 story mode** on PC, up to 4
players. One player hosts in their own save; friends join, appear as an outlaw
riding with them, and can leave at any time. Nothing touches Red Dead Online.

This is **stage 1: players and horses**. Each player sees the others walking,
running and riding in their own world, with the host's time of day. NPCs,
combat and missions are later stages (see the design doc).

> Status: the networking, interpolation and stand-in logic are built and tested
> (unit tests, loopback tests with 4 players, a simulated game world). **It has
> not run inside RDR2 yet.** The first in-game sessions are for finding out
> which parts need tuning - see "First test in game" below.

## What you need

- RDR2 on PC, story mode
- [Script Hook RDR2](http://www.dev-c.com/rdr2/scripthookrdr2/) matching your
  game version (ScriptHookRDR2.dll + dinput8.dll in the game folder)
- The **Script Hook RDR2 SDK** (same page), only to build
- **Visual Studio 2022 or 2026** with "Desktop development with C++", only to build

Back up your saves first: `Documents\Rockstar Games\Red Dead Redemption 2\Profiles`.

## Build

1. Extract the SDK into `sdk\` so these exist:
   `sdk\inc\main.h` and `sdk\lib\ScriptHookRDR2.lib`
2. Double-click `build.bat` (or run it in a terminal). It builds and runs the
   tests. You get `build\Release\RDR2Coop.asi` and `RDR2Coop.ini`.
3. Install: `install.bat "C:\path\to\Red Dead Redemption 2"` (or copy both
   files next to `RDR2.exe`).

## Play

| Key | Does |
|---|---|
| F7 | Host a session (port 7777) |
| F8 | Join `JoinAddress` from the ini |
| F9 | Leave (host leaving ends the session for everyone) |
| F11 | Regroup: teleport next to the host (or, as host, to a player) |
| F10 | Show / hide the status panel |
| F6 | Reload the ini (sync, tuning, display and keys apply live) |

- **Host**: load your save, press F7. The panel shows your LAN address.
- **Friend**: set `JoinAddress` in `RDR2Coop.ini` to the host's address, load
  any save, press F8. You appear behind the host.
- Over the internet: use a VPN like Tailscale or ZeroTier and join the host's
  VPN address (easiest), or forward UDP 7777 on the host's router.
- Windows will ask whether RDR2 may use the network the first time you host:
  allow it.
- Story progress, money and missions happen in the **host's** game. Joiners
  should just ride along, not start missions of their own.

## Testing without friends

`coopbot.exe` makes fake players, so one copy of the game is enough:

```
coopbot selftest                         check networking on this PC (no game needed)
coopbot join --bots 3                    3 bots walk circles around you (you host with F7)
coopbot join --bots 3 --mounted --speed 9   ...on horseback
coopbot join --bots 2 --pattern follow   bots ride behind you in a column
coopbot join --bots 3 --churn 10         a bot leaves and rejoins every 10 s (drop-in/out)
coopbot host                             a fake host that rides around you (you join with F8)
```

Two copies of the game on one PC also work if your PC can launch both: host
with F7 in one, join `127.0.0.1` with F8 in the other. Hotkeys only act in the
focused window.

## First test in game

Do these in order; each one isolates a different part.

1. `coopbot selftest` - networking works on Windows.
2. Load a save, press F7, run `coopbot join --bots 1 --pattern still`.
   **One outlaw should stand still near you.** If not: check the log (below).
3. `--pattern orbit` on foot, then `--mounted --speed 9`. Watch whether they
   walk/ride with animation or slide. If movement looks broken, set
   `OnFootMovement = Teleport` / `MountedMovement = Teleport` and press F6:
   that mode always stays in sync (no animation) and tells us whether the
   problem is the movement tasks or something else.
4. `--bots 3 --churn 10` for joins and leaves.
5. Real second player (second instance or a friend).

Each game launch writes a log to `RDR2Coop_logs\` in the game folder (or to
`%LOCALAPPDATA%\RDR2Coop\logs` if the game folder is read-only, as under
Program Files; a message on screen says so). Every 10 seconds it records sync
quality for each player:

```
sync | Bot1 | ping 1 ms | error avg 0.42 m max 1.10 m | extrapolated 0% | warps 0 | buffer min 38 ms
```

- **error**: how far the stand-in is from where it should be. Under ~1 m on
  foot is good.
- **extrapolated**: how often updates arrived too late. Above a few percent,
  raise `InterpDelayMs`.
- **warps**: times a stand-in was snapped into place because it fell more
  than `TeleportDistance` behind. Should be 0 in normal play.

## How it works (short version)

- One player hosts; clients only talk to the host, which relays everyone's
  position to everyone else (ENet over UDP, 20 updates/s, under 100 bytes each).
- Other players are shown 100 ms in the past so there are always two updates
  to blend between; this hides network jitter.
- Each remote player is a stand-in ped (generic outlaw model, no story role)
  on a horse of the same breed they ride. In Hybrid mode the stand-in gets
  walk/run/ride tasks for proper animation plus small corrections; Teleport
  mode places it exactly every frame.
- Stand-ins are invincible, friendly to you, ignore events, and can't be
  locked on to.

## Project layout

```
core/            portable networking, protocol, interpolation, config (no game code)
plugin/          the .asi: game helpers, stand-ins, overlay, main loop
plugin/natives.list   natives used; tools/gen_natives.py turns it into natives_gen.h
tools/coopbot/   test bots
tests/           unit + loopback tests; tests/sim = simulated game for the plugin logic
config/          default RDR2Coop.ini
third_party/enet ENet 1.3.18 (MIT)
```

Every native hash comes from the community database
(alloc8or/rdr3-nativedb-data) through `tools/gen_natives.py`; none are typed
by hand. To use another native, add its name to `plugin/natives.list` and run
`python tools/gen_natives.py --fetch`.

Developing on Linux/macOS: `cmake -S . -B build && cmake --build build`
builds the core, bots and all tests (including the simulated-game tests for
the stand-ins and for the main loop).
`tools/ci/mingw-toolchain.cmake` cross-compiles a Windows syntax check.

## Troubleshooting

- **Nothing on screen in game**: is `ScriptHookRDR2.dll` + `dinput8.dll` in
  the game folder and up to date for your game version? Script Hook shows its
  own warning on start if it's outdated.
- **"Couldn't host: couldn't open UDP port"**: something else uses 7777
  (another instance already hosting?). Change `HostPort`.
- **Join times out**: wrong `JoinAddress`, host not hosting yet, or a firewall.
  Try `coopbot host` on the host PC and join that to rule out the game.
- **Build error in main.h / nativeInit**: your SDK version may declare the
  Script Hook functions differently; `plugin/native_invoker.h` is the only
  file that calls them.

## Credits

ENet by Lee Salzman (MIT, see `third_party/enet/LICENSE`). Native names and
hashes: alloc8or's RDR3 native database. Model and blip names: femga's
rdr3_discoveries. Script Hook RDR2 by Alexander Blade.
