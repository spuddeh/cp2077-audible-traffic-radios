# Audible Traffic Radios

A RED4ext plugin for Cyberpunk 2077 2.31 that lets you hear the radios playing in traffic cars.

## What it changes

The game only plays a radio station when something other than a traffic car is tuned to it, such
as your own radio or a radio in a shop. A traffic car alone never makes its station play, so the
car's radio stays silent. This plugin lets a traffic car count like any other radio. The game
still plays at most four stations at once, and a traffic radio still sounds like music from inside
a closed car.

The function is resolved by RED4ext hash and every byte is verified before any is written; where a
game update has changed the function, the plugin logs a line and changes nothing.

## Install

Copy `red4ext\plugins\AudibleTrafficRadios\` into the game folder, or install the archive with a
mod manager. Requires [RED4ext](https://www.nexusmods.com/cyberpunk2077/mods/2380).

## Build

See `plugin/CMakeLists.txt`. RED4ext.SDK is header-only; point the include path at a checkout.

## License

Licensed under the [MIT License](LICENSE). Use, change and share this mod and its source,
including in your own mods. Keep the licence notice with any copy.

## Disclaimer

This mod was developed with the assistance of an LLM. All in-game testing and code validation was
performed by a human. No rogue AIs were permitted through the Blackwall.
