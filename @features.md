# Features - Audible Traffic Radios

## Implemented
- A station that only traffic cars are tuned to plays, so a traffic car's radio is heard. The
  patch is verified byte for byte before it is written; on any other game build the plugin logs
  and does nothing.

## Verified in game
- 2026-10-05, Testing: with traffic sound sets receiving through `radio_default_int`, traffic car radios are
  heard, on vanilla and RadioXL stations. The vanilla `radio_car_*_npc` receivers output nothing even at 1.8 m
  with +36 dB.

## Planned
- A car-voiced receiver (car-body muffling, shorter range) in place of the world radio receiver.
- Decide after testing: silence traffic radios during combat only if combat music and car radios
  clash. Vanilla's mode 1 meter reads heard outside combat too, so it cannot be reused for this.
- An open-air sound for convertibles in traffic.
