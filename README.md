# PreySpeedBuff

> [!WARNING]
> This patch has been pretty much entirely been generated using AI; 
> I won't and will never claim to have enough knowledge or expertise to do this 
> kind of reverse-engineering by myself; I've tested briefly and spotted 
> no major issues (on my machine ofc, if you have issues feel free to open an Issue).

A 32-bit ASI plugin that sets the player's movement speed in the Steam release of
Prey (2006) 1.4 from an INI file. It uses the same layout as
[PreyHFR](https://github.com/slopkhayzo/Prey-2006-FPSFix): an external Ultimate
ASI Loader `dinput.dll` plus a self-contained `.asi`. The two can be installed
together and share one loader.

End-user instructions are in
[packaging/PreySpeedBuff-README.txt](packaging/PreySpeedBuff-README.txt).

## Configuration

```ini
[speed]
enabled = true
walk = 270    ; retail 180
run = 330     ; retail 220
crouch = 120  ; retail 80
```

Each speed is in game units per second, from 0 to 100000, or `game` to keep the
game's own value. Invalid or unknown entries leave the plugin inert and are
explained in `PreySpeedBuff.log`.

`walk` is the speed that normally matters. No shipped config binds the engine's
`_speed` button, so `run` only applies if the player binds it.

PreyHFR treats a camera move over 32 units in one 16 ms tic as a cut
(`kMaximumCameraStep`). When the two run together, speeds above about 2000 units
per second lose camera interpolation.

## How it works

Setting the `pm_*speed` cvars from outside would not last. `def/player.def` sets
`pm_walkspeed`, `pm_crouchspeed`, and the stamina cvars, and the game applies
those `pm_` keys to the cvars when the player spawns. Those cvars are also
`CVAR_NETWORKSYNC`, so a changed value could carry into a multiplayer session.

The plugin therefore leaves the cvars alone and wraps `hhPlayer::AdjustSpeed`,
which runs every tic:

1. The retail function runs in full, including stamina and view bob, and hands
   its result to `idPhysics_Player::SetSpeed`.
2. In single player, outside noclip and spectator mode, the hook calls `SetSpeed`
   again with `walk * (1 - f) + run * f`, where `f` is the run fraction retail
   just computed. This is the retail formula with the configured values.

Physics reads the speed from there, so slopes, swimming, ladders, wall walking,
and spirit walking all follow it. Nothing is written to cvars, config files, or
game files. The speed stored in the physics object is included in savegames, but
retail overwrites it on the next tic, so a save made with the plugin also loads
without it.

### Retail mapping

RVAs in `base/gamex86.dll`, SHA-256
`74D436D376BA144762A28C940D0243135B4F9DB8FDD7EE597B9CB5E4277B43C6`:

| RVA | Meaning |
| --- | --- |
| `0x00081490` | `hhPlayer::AdjustSpeed`; called from `0x83f3b` and `0x875e0` |
| `0x0012a960` | `idPhysics_Player::SetSpeed`; its only caller is AdjustSpeed at `0x8168f` |
| `0x00730c44` | `gameLocal.isMultiplayer` |
| `0x0038316c`, `0x003831a0`, `0x003831d4` | `pm_crouchspeed`, `pm_walkspeed`, `pm_runspeed` |

| Offset | Field |
| --- | --- |
| `hhPlayer+0x15b8` | noclip |
| `hhPlayer+0x17e7` | spectating |
| `hhPlayer+0x18a8` | `physicsObj` |
| `hhPlayer+0x1ca8` | run fraction (`bobFrac`) |

### Loading and validation

`DllMain` only starts a bootstrap thread, which reads the INI and registers an
`LdrRegisterDllNotification` callback. The engine can unload and reload
`gamex86.dll`, so the callback hooks each load and forgets the hook on unload.
The plugin also handles a game module that was already loaded when it started.

Before patching, `ValidateGameModule` compares all of `AdjustSpeed` and `SetSpeed`
with the retail bytes, after undoing the image's relocation delta. That also
proves that every absolute reference in the function, including the cvars and
`isMultiplayer`, still names its retail RVA. It also checks section roles and the
three cvar names. Any mismatch, including a function already hooked by another
mod, is logged and leaves the game untouched.

`src/common/adjust_speed_signature.inc` holds the retail bytes and relocation
offsets, generated from the DLL's own `.reloc` table.

## Building

Windows, Visual Studio C++ build tools with an **x86** developer environment, and
CMake 3.21 or newer. From an x86 Visual Studio developer command prompt:

```bat
cmake -S . -B build-x86 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DPREYSPEEDBUFF_RETAIL_GAME_DLL="<game folder>\base\gamex86.dll"
cmake --build build-x86 --parallel
ctest --test-dir build-x86 --output-on-failure
cmake --build build-x86 --target PreySpeedBuffPackage
cmake --build build-x86 --target PreySpeedBuffBundlePackage
```

The ZIPs are written to `build-x86/release/`.

## Tests

- `PreySpeedBuffConfigTest` exercises the INI parser.
- `PreySpeedBuffRetailTest` needs the retail DLL and is skipped without it. It
  loads the real `gamex86.dll` into a test process, without starting the game, and
  runs its real `AdjustSpeed` on a zeroed fake player. The scenarios cover walk,
  run, stamina blend, crouch, noclip, spectator, multiplayer, cvar changes, a
  relocated image, late loading, DLL reload, plugin unload, an altered DLL, and an
  invalid INI.

These tests do not start the engine, so they do not cover actual movement in a
running game.

## Playing a test session

`tools/play_session.ps1` installs the built plugin into the Steam game folder for
one session, starts Prey, waits until it exits, and restores the folder: the
plugin files, PreyHFR if it was switched off, and a run key bound for the
session. The logs go to `research/runs/<time>-<name>/`. `-DryRun` only checks
and prints the plan, and `-RestoreOnly` undoes an interrupted session.

On this machine it is the `prey-2006/speedbuff-play` profile of the shared
queue app, so it never overlaps another agent's game test.

## License

MIT, see [LICENSE](LICENSE). No game files or SDK source are included. Ultimate
ASI Loader is third-party software under its own license in
[vendor/ultimate-asi-loader](vendor/ultimate-asi-loader).
