# Features - Audible Traffic Radios

## Enabled or expanded
Every feature is one of two kinds, and a new one is added here as one or the other.
- **Enabled:** the shipped game already has it (data, sounds, code) and it is held back or broken; the mod makes it
  work and adds nothing of its own.
- **Expanded:** new behaviour, built from the game's own pieces where it can be.

| Feature | Kind | What is the game's own |
| --- | --- | --- |
| Traffic-only stations play | Enabled | the stations, receivers and car radio voicing; one gate byte stops them |
| Other radios play while the player's car radio is on | Enabled | the stations; one rule silences them |
| Morro Rock on the six misspelt lists | Enabled | the lists; a typo in them |
| A level per car (0 to +9 dB, weighted to the middle) | Expanded | the mixer and its `veh_engage_moving_faster` control |
| Muffled from inside a car | Expanded | `veh_interior` and vanilla's traffic-noise values (-4 dB, low-pass 25) |
| Muffled by walls | Expanded | the world radio's own `game_occlusion` curves |
| Open-air cars, graded | Expanded | each car class's EQ (only its treble shelf moves) and the doors, windows and glass the game tracks |
| Through fights and a wanted level | Expanded | combat and police music's own duck on the traffic radio bus, set to 0 |
| Growl FM, Impulse, Dark Star, Royal Blue on themed traffic lists | Expanded | the stations and the list mechanism; the theming follows vanilla's (Delamain, Villefort) |

## Implemented
- [x] A station that only traffic cars are tuned to plays (radio mode 1: one byte in the station's silent
  predicate, `0x2a7831`).
- [x] Other radios keep playing while the player's car radio is on (radio mode 2: the cold branch's `sete al`
  becomes `xor al, al; nop`, reached through the predicate's `jne` at `+0xab`).
- [x] A level per car: the NPC car radio mixer (398448775) at the top of a range (+9 dB), each radio voice lowered
  by its own share (0 to 9 dB) through `veh_engage_moving_faster` set per playing id
  (`SetRTPCValueByPlayingID`, `0x1acf6a0`), the parameter's smoothing bypassed.
  - [x] Weighted to the middle (mean of three hashes).
  - [x] Keyed on the car (`TrafficVehicleEmitter +0x138` entity id), so a restarted radio keeps its level; kind 3
    emitters fall back to the playing id.
  - [x] Every new voice levelled on the next frame; about 1 µs per frame measured.
- [x] Muffled from inside a car: the mixer follows the game's `veh_interior` at vanilla's traffic-noise values
  (-4 dB, low-pass 25). Wwise's lock is tried, never waited on.
- [x] Open-air traffic cars, graded: each car's openness (0 to 1) adds a weight per window down (0.25), door open
  (0.5), door torn off (0.75) and shattered pane (0.25, `Glass::IsShattered`, `0x273094`); 1 for no side windows
  (`hasSideWindows` false, or a `targa`/`cabrio` `player_audio_resource`). Doors and windows read through RTTI every
  250 ms; kind 3 cars stay closed.
  - [x] Openness is `atr_open_air` (this plugin's own game parameter) on the car's Wwise game object, 250 ms glide.
  - [x] Only each receiver EQ's treble shelf fades, to 0 dB at 1 (the closed body); bass, mid and reverb bands keep
    CDPR's sound per car class. Curves attached to the effects at load (`CAkFxBase` `SetRTPC`, `0x1b50b20`):
    exclusive, absolute, unscaled, five points.
  - [x] The EQ output takes off the loudness the lift adds (6.9 / 5.9 / 2.4 / 1.0 dB by receiver) and adds 3 dB,
    so every open car type is 3 dB louder and the balance between types holds.
- [x] Muffled by walls: the world radio's two `game_occlusion` curves (volume 0 to -12 dB, low-pass 0 to 57)
  attached to the NPC mixer once at load (`SetRTPC` virtual, `0x1adda50`).
- [x] Through fights and a wanted level: the -96 dB duck that combat and police music put on the NPC radio bus is
  set to 0 on both ducking buses (`+0x110` list, entry target `+0x08`, volume `+0x0c`).
- [x] Every address and byte is verified before use; on any other game build the plugin logs and does nothing.
- [x] Tuning build (`ATR_TUNE`): `atr_tune.txt` (`levels`, `muffle`, `open 1` for every car fully open,
  `weights <window> <door> <torn-off> <pane>`) applied live; every 10 s a `perf:`, `cars:` and `playing:` line
  (each car's level and openness); a `level:` line as each radio starts; an EQ view (a `SetParam` hook logging
  every gain, Q and output the Parametric EQs receive); `atr_live.txt` for the Radio Probe Overlay, which draws
  each car's receiver, level and openness on it.
- [x] `docs/HOW-IT-WORKS.md`: the technical write-up for modders.

## Verified in game (Testing)
- 2026-10-07: both duck lines logged at load; traffic radios heard through a wanted level and a fight by ear.
- 2026-10-05: traffic car radios heard at the game's own car radio voicing, on vanilla and RadioXL stations; the
  mixer object keeps one address through save loads and the main menu.
- 2026-10-06: per-car levels land (`/t3`, per playing id); 14 of 14 restarted cars came back at the same level;
  no glide after the bypass; hijacking hands over to the player's receiver within 0.3 s; a car leaving traffic
  (kind 3) keeps playing after a 0.1 s cut; police music silences traffic radios (metered); other radios play in
  the player's car; levels 0 to 12 chosen by ear, then the ceiling lowered to +9 by ear (some cars too loud).
- 2026-10-06: the try-the-lock muffling's `perf:` line averages 1.8 to 2.7 µs with radios playing (worst 13 to
  184 µs); with open-air cars and occlusion, 3.5 to 5 µs.
- 2026-10-06: walls muffle traffic radios (the Japantown apartment: `game_occlusion` 1.00 on every traffic radio,
  no longer heard indoors; 0.00 to 0.90 on the street).
- 2026-10-06: open-air cars: every car found as a vehicle (93 of 93 reads); a Thorton Galena switched clear when
  its driver's window and door were opened from the live bridge and back when closed, three door rounds in a row.
- 2026-10-06: a Mizutani Shion went open on a shattered pane and stayed open; a Chevalier Emperor went open when
  its front-left door was torn off (both from the live bridge, heard by the user).
- 2026-10-06: the graded fade, read through the EQ view and heard on sports (Quadra Turbo), truck (Kaukaz Zeya) and
  low-end (Thorton Galena) cars: the treble shelf steps evenly (-20 to 0, -24 to 0), the other bands hold, one
  shattered pane and a torn-off door step the same way.
- 2026-10-06: in-car muffling lands exactly (first person: mixer +9 to +5 dB, low-pass 0 to 25, back in third
  person, about 1 s each way), heard by the user. Scan cost with open-air cars: 1.6 to 6 µs average (levels and
  muffling about 3, reading the cars up to 1.4, open-air up to 0.8); the tuning build's overlay file write had
  added 10 to 25 µs and now runs outside the timed scan.

## Pending checks

## Planned
- [x] A preview build for the author of Immersive NPC Car Stereos: GitHub pre-release `v0.1.0-preview`
  (2026-10-06), the release DLL verified in game first.
- [ ] Open-air cars by ear in normal play: the weights, the +3 dB rise, muscle and police receivers.
- [ ] A versioned API for Immersive NPC Car Stereos, shaped by that author's feedback.
- [ ] More station variety on traffic cars.
