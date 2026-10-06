# Changelog - Audible Traffic Radios

### [2026-10-06] Session
- **[AudibleTrafficRadios] [Main.cpp] v0.1.0**:
    - [New] Radio mode 2 patch: the station predicate's cold branch (`sete al` -> `xor al, al; nop`, found through
      the `jne` at `+0xab`) plays a station with no player receiver while the player's car radio is on.
    - [New] A level per car: the NPC mixer at the top of the range (+12 dB, was +18); each NPC receiver voice
      (kinds 2 and 3, broadcast event `+0x120` matched in the emitter's sounds) gets `veh_engage_moving_faster`
      through `SetRTPCValueByPlayingID` (`0x1acf6a0`), mapped from dB through the curve's S-shape.
    - [New] Levels weighted to the middle (mean of three 64-bit hashes) and keyed on the traffic car's entity id
      (`TrafficVehicleEmitter +0x138`, vtable `0x2b458a0`); kind 3 falls back to the playing id.
    - [Fix] The per-voice setter bypasses the parameter's speed smoothing (no 3 s swell on every restart).
    - [Refactor] The voice scan runs every frame instead of every 250 ms.
    - [New] In-car muffling: `veh_interior` (290459857) read through `Query::GetRTPCValue` (`0x1ad2c60`); the
      mixer's volume and low-pass (property 2) follow it at -4 dB and 25.
    - [Fix] The muffling tries Wwise's global lock (`TryEnterCriticalSection` on the section `CAkFunctionCritical`
      enters, found from its `lea` at `+0x17`) and skips a busy frame; the blocking version stalled frames up to
      28 ms.
    - [New] `ATR_TUNE` build: `atr_tune.txt` (`levels <bottom> <top>`, `muffle <dB> <lpf>`) every 2 s, and a
      `perf:` line with the scan's average and worst cost every 10 s.
- **[AudibleTrafficRadios] [docs/HOW-IT-WORKS.md, README.md]**:
    - [New] Technical write-up for modders; README describes the current behaviour.

### [0.1.0] - unreleased
- RED4ext plugin. Patches the radio station's silent predicate so a station that only traffic cars are tuned to
  plays in radio mode 1 (resolved by hash `2590772874`, twenty bytes verified, one byte written).
- Sets the NPC car radio mixer's volume to +18 dB on the loaded Wwise object through
  `CAkParameterNode::SetAkProp`, once, under Wwise's global lock, only over the vanilla -8 dB.
- No bank ships. `plugin/dev` keeps the dev harness (test banks, receiver repointing, meters, node dumps) and
  `tools/` the bank builders; neither is built into the mod.
