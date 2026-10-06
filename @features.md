# Features - Audible Traffic Radios

## Implemented
- [x] A station that only traffic cars are tuned to plays (radio mode 1: one byte in the station's silent
  predicate, `0x2a7831`).
- [x] Other radios keep playing while the player's car radio is on (radio mode 2: the cold branch's `sete al`
  becomes `xor al, al; nop`, reached through the predicate's `jne` at `+0xab`).
- [x] A level per car: the NPC car radio mixer (398448775) at the top of a range (+12 dB), each radio voice lowered
  by its own share (0 to 12 dB) through `veh_engage_moving_faster` set per playing id
  (`SetRTPCValueByPlayingID`, `0x1acf6a0`), the parameter's smoothing bypassed.
  - [x] Weighted to the middle (mean of three hashes).
  - [x] Keyed on the car (`TrafficVehicleEmitter +0x138` entity id), so a restarted radio keeps its level; kind 3
    emitters fall back to the playing id.
  - [x] Every new voice levelled on the next frame; about 1 µs per frame measured.
- [x] Muffled from inside a car: the mixer follows the game's `veh_interior` at vanilla's traffic-noise values
  (-4 dB, low-pass 25). Wwise's lock is tried, never waited on.
- [x] Every address and byte is verified before use; on any other game build the plugin logs and does nothing.
- [x] Tuning build (`ATR_TUNE`): `atr_tune.txt` (`levels`, `muffle`) applied live, and a `perf:` line every 10 s.
- [x] `docs/HOW-IT-WORKS.md`: the technical write-up for modders.

## Verified in game (Testing)
- 2026-10-05: traffic car radios heard at the game's own car radio voicing, on vanilla and RadioXL stations; the
  mixer object keeps one address through save loads and the main menu.
- 2026-10-06: per-car levels land (`/t3`, per playing id); 14 of 14 restarted cars came back at the same level;
  no glide after the bypass; hijacking hands over to the player's receiver within 0.3 s; a car leaving traffic
  (kind 3) keeps playing after a 0.1 s cut; police music silences traffic radios (metered); other radios play in
  the player's car; levels 0 to 12 chosen by ear.

## Pending checks
- [ ] The `perf:` line with the try-the-lock muffling (the blocking version measured 30 to 180 µs and stalls up to
  28 ms).
- [ ] The muffling at vanilla's values, by ear.

## Planned
- [ ] Open-air traffic cars: a car with a door open or detached, or a window down, plays through an open receiver
  voicing (Priority: High).
- [ ] A preview build for the author of Immersive NPC Car Stereos (Priority: High, after open-air cars).
- [ ] A versioned API for Immersive NPC Car Stereos, shaped by that author's feedback.
- [ ] More station variety on traffic cars.
