# Features - Audible Traffic Radios

## Implemented
- A station that only traffic cars are tuned to plays, so a traffic car's radio is heard. The
  patch is verified byte for byte before it is written; on any other game build the plugin logs
  and does nothing.

## Planned
- Keep traffic radios quiet during combat and police music, as vanilla mode 1 intends.
- Raise the NPC receiver level if traffic radios are still too quiet (`veh_engage_moving_faster`).
- An open-air sound for convertibles in traffic.
