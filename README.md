# Audible Traffic Radios

A RED4ext plugin for Cyberpunk 2077 2.31 that lets you hear the radios playing in traffic cars, through the
game's own car radio sound.

## What it changes

Some of this is the game's own and only held back; some is new.

**The game's own, made to work:**

- **Traffic radios play.** A station that only traffic cars are tuned to plays. Without this, a traffic car's
  radio is tuned but its station never plays.
- **Other radios keep playing while your car radio is on.** The game otherwise silences every station your own
  car radio is not tuned to: traffic, world radios and street music.
- **Morro Rock on every list that names it.** Six cars' station lists misspell Morro Rock, so those cars never
  play it.

**New:**

- **Each car has its own level.** Traffic radios play between +0 and +9 dB over the game's own car radio level,
  most near the middle and a few quiet or loud. A car keeps its level whenever its radio restarts.
- **Muffled from inside a car.** In first person in a car, traffic radios are muffled the way the game already
  muffles traffic noise (-4 dB and a low-pass filter). Third person, or broken glass, lets more through.
- **Muffled by walls.** Walls muffle traffic radios the way they muffle the game's other radios.
- **Traffic radios keep playing through fights and a wanted level.** The game silences them while combat or
  police music plays; your own radio still follows the game's rules (or RadioXL's switches).
- **Open cars sound open.** A car with a door open or torn off, a window down or broken, or no roof, sounds less
  muffled and a little louder the more of it is open. Each car class keeps its own sound.
- **More stations on traffic.** Growl FM, Impulse, Dark Star and Royal Blue Radio, which no traffic car plays in
  the base game, are on themed cars: Growl FM on most cars, Impulse on sports cars, Dark Star on gang and nomad
  cars, Royal Blue Radio on executive cars and limousines.

What you hear is the game's own car radio sound: each car class's own filtering, so it sounds like music from
inside a car with the windows up, and the game's own distance falloff, fading out by about 35 m. Dialogue is
set to lower them, as in the base game. A car that has left traffic (one you get out of, or one hit, fleeing or abandoned) keeps the closed
sound with its doors open, and its level is picked again as it leaves traffic.

No sound bank is shipped or replaced: the changes are made to the sound data the game has loaded. Every address is
verified before it is used, and on any other game build the plugin logs a line and changes nothing.

**How it works, for modders:** [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md) - the game's radio gate, the mix,
and the Wwise techniques (editing loaded objects in memory, per-voice parameters, the audio lock).

## Install

Copy `red4ext\plugins\AudibleTrafficRadios\` into the game folder, or install the archive with a mod manager.
Requires [RED4ext](https://www.nexusmods.com/cyberpunk2077/mods/2380).

## Build

```powershell
cmake -S plugin -B plugin\build -G "Visual Studio 17 2022" -A x64
cmake --build plugin\build --config Release
```

RED4ext.SDK is header-only; `plugin/CMakeLists.txt` points the include path at a checkout. Configure with
`-DATR_TUNE=ON` for the tuning build, which reads `atr_tune.txt` beside the DLL and logs its own per-frame cost.
It is for development only.

## License

Licensed under the [MIT License](LICENSE). Use, change and share this mod and its source,
including in your own mods. Keep the licence notice with any copy.

## Disclaimer

This mod was developed with the assistance of an LLM. All in-game testing and code validation was
performed by a human. No rogue AIs were permitted through the Blackwall.
