# Upstream source

The files under `code/game` and `code/ui` were imported from the official
id Software Quake III Arena GPL source release:

- Repository: https://github.com/id-Software/Quake-III-Arena
- Commit: `dbe4ddb10315479fc00086f08e25d968b4b43c49`
- Imported paths: `code/game`, base-game `code/q3_ui`, and the base UI's
  `code/ui/ui_public.h` and `code/ui/ui_syscalls.c`
- License: GNU General Public License version 2 or later; see `COPYING.txt`

The imported `code/q3_ui` directory is named `code/ui` locally. Build-system
files are local; the C and header files remain the editable implementation
baseline for the integration.

The native entry points and syscall callback use `intptr_t` so pointers are not
truncated by the original 32-bit-only DLL ABI when building for x86-64. The
obsolete global-ranking sources are not part of the normal base-game targets.
