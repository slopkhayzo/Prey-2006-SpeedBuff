PreySpeedBuff 1.0.0
===================

PreySpeedBuff is an unofficial 32-bit ASI plugin for the Windows Steam release
of Prey (2006) version 1.4. It sets the player's movement speed from an INI file.

No original game files are replaced or modified.


QUICK INSTALL - BUNDLE WITH LOADER
----------------------------------

1. Close Prey if it is running.

2. Check the folder containing prey.exe for an existing dinput.dll. Do not
   overwrite one: it may belong to another mod or ASI loader. If you already
   use PreyHFR, you have a compatible loader; follow PLUGIN-ONLY INSTALL.

3. If no dinput.dll exists, extract every file from the "with-loader" ZIP into
   that folder. It includes the Win32 dinput.dll build of Ultimate ASI Loader
   v9.7.4, its dinput.ini, and its MIT license.

4. Launch Prey normally through Steam or prey.exe.


PLUGIN-ONLY INSTALL
-------------------

Use the plugin-only ZIP if the game already has a compatible 32-bit ASI loader.
Copy PreySpeedBuff.asi and PreySpeedBuff.ini beside prey.exe, or into another
ASI directory supported by that loader while keeping both files together.


CHANGING THE SPEED
------------------

Open PreySpeedBuff.ini in Notepad and restart the game after saving it.

    walk = 270
    run = 330
    crouch = 120

Speeds are in game units per second, from 0 to 100000. The single-player retail
values are walk 180, run 220, and crouch 80. Use "game" for any value you do
not want to change. Set enabled = false to turn the plugin off.

walk is the speed you normally move at, so it is the value to change. The game
binds no run key by default, so run only matters if you bind one, for example
with the console command:

    bind "CTRL" "_speed"

The game's stamina still fades run back to walk as it runs out. Spirit walking
and wall walking use these same speeds.

Noclip, spectator mode, vehicles, jump height, and gravity are not changed.
Very high speeds have not been tested across the whole game.

With PreyHFR's camera interpolation, stay at or below about 2000. Above that,
PreyHFR treats each game tic as a camera cut, so motion loses its smoothing.


MULTIPLAYER
-----------

The speed override never applies in a multiplayer session, but the plugin stays
loaded and its code hook remains in the game's memory. It has not been tested
with PunkBuster, so remove PreySpeedBuff.asi before playing online.


TROUBLESHOOTING
---------------

- Check PreySpeedBuff.log beside the ASI. Invalid or unknown INI entries, or a
  game build that differs from retail 1.4, leave the game's speed unchanged and
  are explained there.
- If the log is not created, the ASI loader did not discover the plugin; check
  loader placement and architecture first.
- A line reading "first override applied" means the speed is active in-game.

For source code, updates, and issue reports, visit:

https://github.com/slopkhayzo/Prey-2006-SpeedBuff


SUPPORTED GAME VERSION
----------------------

base\gamex86.dll from Steam Prey 1.4, SHA-256:
74D436D376BA144762A28C940D0243135B4F9DB8FDD7EE597B9CB5E4277B43C6

This hash records what was tested and is not checked directly. The plugin instead
compares the code it hooks against retail on every launch, and stays inactive if
it differs.


UNINSTALLING
------------

Close the game and delete PreySpeedBuff.asi, PreySpeedBuff.ini,
PreySpeedBuff.log, PreySpeedBuff-README.txt, PreySpeedBuff-LICENSE.txt, and
PreySpeedBuff-release.json. Loader-bundle files such as dinput.dll are shared
third-party infrastructure; remove them separately only after checking that no
other ASI plugin depends on them.


LICENSE
-------

PreySpeedBuff is distributed under the MIT License. See
PreySpeedBuff-LICENSE.txt.

The loader-inclusive bundle also contains Ultimate-ASI-Loader-LICENSE.txt,
Ultimate-ASI-Loader-NOTICE.txt, and a checksum record for the pinned binary.
Ultimate ASI Loader is third-party software by ThirteenAG and is not part of
PreySpeedBuff.

Prey is a trademark of its respective owners. This unofficial project is not
affiliated with or endorsed by Bethesda Softworks, ZeniMax Media, Human Head
Studios, or 3D Realms.
