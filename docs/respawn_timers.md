# Respawn timers

Timers work with the automap overlay in baseq3 AP and CPMA AP.

- `ap_show_respawntimer 1`: enable timers (default); `0` hides them.
- `ap_minrespawntimer 20`: show respawn cycles lasting at least 20 seconds
  (default). Once shown, the countdown continues down to zero. Set `0` to
  include short respawns too. This changes the display, not respawn speeds.
- `ap_show_all_respawns 0`: only unchecked AP pickup locations (default).
- `ap_show_all_respawns 1`: also include ordinary and already-checked map pickups,
  including ammo. This does not bring back checked-location boxes or orbs.
- `ap_timer_throughwalls 1`: draw timers through walls (default).
  Set `0` to hide timers behind solid walls and closed doors. This only affects
  timer text, not automap boxes or orbs.

All settings are saved. Automap must be enabled; locked pickups remain
hidden. Timers use the server's scheduled respawn time, including CPMA 1.53's
private item schedule. An unavailable schedule on an unsupported CPMA layout
may use an estimate, shown with `<=`. An item without a scheduled respawn has
no exact countdown.
