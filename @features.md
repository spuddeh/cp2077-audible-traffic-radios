# Features - Audible Traffic Radios

## Implemented
- A station that only traffic cars are tuned to plays (one byte in the station's silent predicate).
- The NPC car radio mixer (Wwise actor mixer 398448775) plays at +18 dB instead of -8 dB, set once on the loaded
  object through Wwise's own property setter. No bank. Guarded by the parameter-node vtable and the vanilla value.
- Every address and byte is verified before use; on any other game build the plugin logs and does nothing.

## Verified in game
- 2026-10-05, Testing: traffic car radios heard at a natural level, the game's own car radio voicing, on vanilla
  and RadioXL stations. The release build logs `patched:` and `the NPC car radio mixer plays at 18.000000 dB`,
  once each. The mixer object keeps one address through save loads and the main menu.

## Planned
- A range of levels per car, so traffic radios do not all sound the same.
- More station variety on traffic cars.
- Decide after testing: silence traffic radios during combat only if combat music and car radios clash.
- An open-air sound for convertibles (only the Shion Targa spawns in vanilla traffic, about 1 in 500 cars where it
  can).
