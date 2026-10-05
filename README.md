# Audible Traffic Radios

A RED4ext plugin for Cyberpunk 2077 2.31 that lets you hear the radios playing in traffic cars, through the
game's own car radio sound.

## What it changes

Two things in the game's own radio, nothing else:

- A station that only traffic cars are tuned to plays. Without this, a traffic car's radio is tuned but its
  station never plays, so the car stays silent.
- The NPC car radio mixer plays 26 dB louder (-8 dB to +18 dB), set on the loaded sound data in memory. No
  sound bank is shipped or replaced.

What you hear is the game's own car radio sound: each car class's own filtering, so it sounds like music from
inside a car with the windows up, and the game's own distance falloff, fading out by about 35 m.

Every address is verified before it is used, and the mixer value is only changed when it still holds the game's
own -8 dB. On any other game build the plugin logs a line and changes nothing.

## Install

Copy `red4ext\plugins\AudibleTrafficRadios\` into the game folder, or install the archive with a mod manager.
Requires [RED4ext](https://www.nexusmods.com/cyberpunk2077/mods/2380).

## Build

See `plugin/CMakeLists.txt`. RED4ext.SDK is header-only; point the include path at a checkout.

## License

Licensed under the [MIT License](LICENSE). Use, change and share this mod and its source,
including in your own mods. Keep the licence notice with any copy.

## Disclaimer

This mod was developed with the assistance of an LLM. All in-game testing and code validation was
performed by a human. No rogue AIs were permitted through the Blackwall.
