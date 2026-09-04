# Quake III Archipelago client

This hybrid client keeps the Archipelago/APCc connection in Quake3e while GPL
native qagame and UI modules implement checks, unlocks, and menus. The retail
assets are not included.

Build and install with the commands in `BUILD.md`, then run `q3ap-launch.cmd`.
Choose **ARCHIPELAGO** on the main menu, enter the server, port, slot, and
optional password, connect, and open **Stages**. Only selected stages appear;
received stage-access items unlock them. Cleared and out-of-logic stages remain
replayable.

The console fallback is `ap_connect <host> <port> <slot>`, followed by
`ap_status`, `ap_maps`, or `ap_start_stage <mapkey>`. Use the menu for
password-protected rooms because console history is not suitable for secrets.

Version 0.1 supports stock Quake III Arena maps, one local human, native x64
modules, pickup checks, credited bot kills, and stage clears. QVM mode and
multiplayer humans are not supported. A valid retail pak0 and the user-owned
1.32 point-release PK3s are required.

## Module-only development build

Configure and build on 64-bit Windows with CMake and MSVC:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix ..\dist
```

The install step writes `qagamex86_64.dll` and `uix86_64.dll` to
`..\dist\q3ap`.
