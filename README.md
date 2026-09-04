# Quake3e

[![build](../../workflows/build/badge.svg)](../../actions?query=workflow%3Abuild) * <a href="https://discord.com/invite/X3Exs4C"><img src="https://img.shields.io/discord/314456230649135105?color=7289da&logo=discord&logoColor=white" alt="Discord server" /></a>

This is a modern Quake III Arena engine aimed to be fast, secure and compatible with all existing Q3A mods.
It is based on last non-SDL source dump of [ioquake3](https://github.com/ioquake/ioq3) with latest upstream fixes applied.

Go to [Releases](../../releases) section to download latest binaries for your platform or follow [Build Instructions](#build-instructions)

*This repository does not contain any game content so in order to play you must copy the resulting binaries into your existing Quake III Arena installation*

## Archipelago custom console settings

For release installation and play, see the APWorld's `docs/setup_en.md`
included in the client ZIP. Release candidate packaging is described in
`docs/release_candidate.md`.

These are the custom settings added by this AP fork, not the complete Quake3e
or CPMA cvar list. Defaults below are code defaults; an existing config or
`autoexec.cfg` can override them. Enter `/cvar_name value` in the game console.
Settings marked **Yes** are archived automatically in the active mod's config.

| Cvar | Default | Saved | Purpose / values |
| --- | --- | --- | --- |
| `ap_automap` | `1` | Yes | `1`: show AP bounding boxes; `0`: hide them. Respawn timer text also requires automap to be on. Does not hide the floating item-classification models. |
| `ap_show_respawntimer` | `1` | Yes | `1`: show respawn countdowns; `0`: hide timer text without disabling boxes. |
| `ap_minrespawntimer` | `20` | Yes | Minimum **total respawn duration**, in seconds, for displaying a timer (`0`–`600`). `0` includes short respawns. A qualifying countdown stays visible down to zero; this does not change respawn speed. |
| `ap_show_all_respawns` | `0` | Yes | `0`: timers only for unchecked AP pickup locations. `1`: also show timers for ordinary and already-checked map pickups, including ammo. Locked pickups remain hidden; checked-location boxes/orbs do not return. |
| `ap_timer_throughwalls` | `1` | Yes | `1`: show timer text through walls. `0`: require an unobstructed trace to the pickup; solid walls and closed doors block it. Only affects timer text. |
| `ap_chat_messages` | `0` | Yes | `0`: AP notifications in the console. `1`: route them through in-game chat while playing, with console fallback outside gameplay. The chat path avoids an extra duplicate console print and sets the existing `cg_nochatbeep` to `1`. |
| `ap_progression_sound` | `1` | Yes | Play `sound/misc/menu2.wav` on received progression-item notifications. `0` disables it. Sending an item alone does not trigger it. |
| `ap_minnotify` | `0` | Yes | Filter sent/received item notifications in both console and chat: `0` = all; `1` = hide filler; `2` = hide filler and useful-only items. Progression and traps remain visible. Hints and other non-item messages are unaffected. |
| `cg_skillspread` | `10` | Yes | Random integer skill offset per bot added by AP stage launch: `-spread` through `+spread`. Effective range `0`–`94`; `0` disables variation. Used only when the base `g_spSkill` is at least `6` (CPMA's extended skill range). |
| `cg_maxskill` | `100` | Yes | Upper clamp for AP-spawned bots in that extended skill range; effective range `6`–`100`. Lower bound stays `6`. Does not retune bots already in the map. |
| `ap_debug_pickups` | `0` (unset) | No | CPMA pickup/marker/timer diagnostics, including the white `CPMA sent pickup ...` lines. `1` enables verbose logging; `0` disables it. This is read on demand, not explicitly registered or archived. |
| `ap_debug_timing` | `0` | No | `1`: log AP initialization, connection-status, slot-data readiness, and scout timings; `0`: off. |
| `ui_apHost` | `localhost` | Yes | Host prefilled in the Archipelago connection menu. Exact `localhost` is normalized to `127.0.0.1` when connecting. |
| `ui_apPort` | `38281` | Yes | Port prefilled in the connection menu; accepted connection range `1`–`65535`. The local test setup overrides this to `38282`. |
| `ui_apSlot` | empty | Yes | Slot name prefilled in the connection menu. The local test setup uses `Ranger`. |

The overlay and notification settings work in both baseq3 AP and CPMA AP;
`ap_debug_pickups` is CPMA-specific. Timer cvars are registered when the game
first draws a scene, and menu cvars when the connection menu opens. You can
still set them beforehand with `set` or `seta`. Non-archived diagnostics can
be explicitly saved with `seta`, but verbose logging is best left off normally.

### Bot skill and other existing settings

`g_spSkill` is an existing game cvar, not a new AP cvar. AP stage launch reads it
as the base bot skill. For example:

```text
/g_spSkill 80
/cg_skillspread 10
/cg_maxskill 85
```

With CPMA, each bot added on the next AP stage launch receives a randomized
skill from 70 through 90, clamped to 85 (so multiple rolls can become 85).
The spread is per bot at launch, not continually rerolled and not applied to
manual `addbot` commands. Stock baseq3 uses its normal 1–5 skill range.

CPMA appearance settings such as `cg_enemyModel`, `cg_enemyColors`, and
`cg_forceModel`, plus engine settings such as `cl_renderer`, `in_nograb`,
and `s_show`, are existing upstream settings, not custom AP additions.
The `AP: X/Y` check-count HUD currently has no separate custom toggle.
Seed-generation options (map pools, item percentages, goals, and weapon logic)
belong in the AP YAML, not console cvars.

### Related console commands (not cvars)

| Command | Purpose |
| --- | --- |
| `automap` | Toggle `ap_automap`. Bind it with `/bind m automap`, or use the controls menu's Automap binding. |
| `ap_connect <host> <port> <slot>` | Connect to Archipelago. Quote slot names containing spaces. Use the menu for password-protected rooms. |
| `ap_disconnect` | Disconnect/cancel the current AP connection attempt. |
| `ap_status` | Print current connection/slot-data status. |
| `ap_maps` | List the seed's stages and their locked/unlocked/cleared status. |
| `ap_start_stage <mapkey>` | Start a selected, unlocked stage after connection and synchronization are ready. |
| `ap_say <message>` | Send a chat message to the AP server. |
| `ap_cpma_stage_limits` | Internal stage-launch helper: apply the current catalogued map's frag target and zero time limit through the CPMA 1.53 adapter. Normally called automatically, not needed as a player setting. |

See also [respawn timer details](docs/respawn_timers.md) and
[AP module setup](apgame/README.md).

## Engine features

**Key features**:

* optimized OpenGL renderer
* optimized Vulkan renderer
* raw mouse input support, enabled automatically instead of DirectInput(**\in_mouse 1**) if available
* **\in_minimize** - hotkey for minimize/restore main window (win32-only, direct replacement for Q3Minimizer)
* **\video-pipe** - to use external ffmpeg binary as an encoder for better quality and smaller output files
* significally reworked QVM (Quake Virtual Machine)
* improved server-side DoS protection, much reduced memory usage
* raised filesystem limits (up to 20,000 maps can be handled in a single directory)
* reworked Zone memory allocator, no more out-of-memory errors
* non-intrusive support for SDL2 backend (video, audio, input), selectable at compile time
* tons of bug fixes and other improvements

## Vulkan renderer

Based on [Quake-III-Arena-Kenny-Edition](https://github.com/kennyalive/Quake-III-Arena-Kenny-Edition) with many additions:

* high-quality per-pixel dynamic lighting
* very fast flares (**\r_flares 1**)
* anisotropic filtering (**\r_ext_texture_filter_anisotropic**)
* greatly reduced API overhead (call/dispatch ratio)
* flexible vertex buffer memory management to allow loading huge maps
* multiple command buffers to reduce processing bottlenecks
* [reversed depth buffer](https://developer.nvidia.com/content/depth-precision-visualized) to eliminate z-fighting on big maps
* merged lightmaps (atlases)
* multitexturing optimizations
* static world surfaces cached in VBO (**\r_vbo 1**)
* useful debug markers for tools like [RenderDoc](https://renderdoc.org/)
* fixed framebuffer corruption on some Intel iGPUs
* offscreen rendering, enabled with **\r_fbo 1**, all following requires it enabled:
* `screenMap` texture rendering - to create realistic environment reflections
* multisample anti-aliasing (**\r_ext_multisample**)
* supersample anti-aliasing (**\r_ext_supersample**)
* per-window gamma-correction which is important for screen-capture tools like OBS
* you can minimize game window any time during **\video**|**\video-pipe** recording
* high dynamic range render targets (**\r_hdr 1**) to avoid color banding
* bloom post-processing effect
* arbitrary resolution rendering
* greyscale mode

In general, not counting offscreen rendering features you might expect from 10% to 200%+ FPS increase comparing to KE's original version

Highly recommended to use on modern systems

## OpenGL renderer

Based on classic OpenGL renderers from [idq3](https://github.com/id-Software/Quake-III-Arena)/[ioquake3](https://github.com/ioquake/ioq3)/[cnq3](https://bitbucket.org/CPMADevs/cnq3)/[openarena](https://github.com/OpenArena/engine), features:

* OpenGL 1.1 compatible, uses features from newer versions whenever available
* high-quality per-pixel dynamic lighting, can be triggered by **\r_dlightMode** cvar
* merged lightmaps (atlases)
* static world surfaces cached in VBO (**\r_vbo 1**)
* all set of offscreen rendering features mentioned in Vulkan renderer, plus:
* bloom reflection post-processing effect

Performance is usually greater or equal to other opengl1 renderers

## OpenGL2 renderer

Original ioquake3 renderer, performance is very poor on non-nvidia systems, unmaintained

## [Build Instructions](BUILD.md)

## Contacts

Discord channel: https://discordapp.com/invite/X3Exs4C

## Links

* https://bitbucket.org/CPMADevs/cnq3
* https://github.com/ioquake/ioq3
* https://github.com/kennyalive/Quake-III-Arena-Kenny-Edition
* https://github.com/OpenArena/engine
