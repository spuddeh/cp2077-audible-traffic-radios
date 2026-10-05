# Changelog - Audible Traffic Radios

### [0.1.0] - unreleased
- RED4ext plugin. Patches the radio station's silent predicate so a station that only traffic cars are tuned to
  plays in radio mode 1 (resolved by hash `2590772874`, twenty bytes verified, one byte written).
- Sets the NPC car radio mixer's volume to +18 dB on the loaded Wwise object through
  `CAkParameterNode::SetAkProp`, once, under Wwise's global lock, only over the vanilla -8 dB.
- No bank ships. `plugin/dev` keeps the dev harness (test banks, receiver repointing, meters, node dumps) and
  `tools/` the bank builders; neither is built into the mod.
