# Changelog - Audible Traffic Radios

### [2026-11-02] Session
- **[AudibleTrafficRadios] [Build.hpp, Wwise.cpp, Main.cpp, every feature file] v1.1.0**:
    - [New] Game 2.5 support. Every address outside the two hash-resolved radio functions moves to `Build.hpp`,
      one table per game build (2.5, 2.31), and `SelectBuild` uses the table whose every byte check passes; the
      game version is only logged. The 2.5 table was matched from 2.31's in the Steam 2.5 exe (functions matched
      with relocations masked, each entry confirmed by its 2.5 symbol name or by the code that references it).
      Moved: every Wwise function and global (`g_pIndex` 0x39c5d00 and `g_pRegistryMgr` 0x39c5d08 swapped order),
      the parameter-node, `CAkFxCustom` and `TrafficVehicleEmitter` vtables, engine root -> audio system
      (+0xa8 -> +0xc8), vehicle -> destruction (+0x600 -> +0x620); `CAkFxBase::AddRtpcCurve` has a new prologue.
      Unchanged: Wwise's index, bus duck lists and property bundles, and the radio emitter fields.
    - [Fix] 1.0.0 declared `RUNTIME_VERSION_INDEPENDENT` and was built against the 2.31 SDK, so on 2.5 it ran
      2.31's `CStackFrame` (0x68 bytes; 2.5's is 0xA8) in `ExecuteFunction` and read `CGameEngine::framework` at
      +0x308 (2.5: +0x348), and six of nine features refused at their byte checks. It now declares the latest
      runtime and builds once per game build.
    - [New] A DLL per game build through the shared plugin build standard (`plugin/build.ps1`,
      `red4ext-plugin.cmake`, `builds.json`: 2.5 on SDK 5246e659, 2.31 on f9ad5334). The 2.31 DLL ships as the
      second main file "Audible Traffic Radios - Game 2.31" (artifact `game231`); a player installs one or the
      other. Tuning build: `build.ps1 -Variant tune`.
- **[AudibleTrafficRadios] [Stations.cpp] v1.1.0**:
    - [New] HKGN (`radio_station_15_edgerunners`) on every traffic list that holds the nine shared stations, as
      Growl FM is; added only where the `RadioStation.HKGN` record exists, so 2.31 is untouched. 2.5: 381 stations
      added across 169 lists (226 before); 2.31: 223 across 166, as 1.0.0.
    - [Verified] Testing (2.5): every byte check passes, all nine features attach, traffic radios heard, 2 of 9
      tuned cars at a roundabout on HKGN. Gameplay (2.31): every byte check passes, all nine features attach,
      stable in the world.

### [2026-10-09] Session
- **[AudibleTrafficRadios] [Main.cpp] v1.0.0**:
    - [Change] Version 1.0.0 for the first release (plugin info and source header).
    - [New] Release pipeline: `release.yml` (shared copy), `release-manifest.json`, and `publish.json` for the
      private dev repo / public release repo split.

### [2026-10-07] Session
- **[AudibleTrafficRadios] [Levels.cpp, OpenAir.cpp, Muffling.cpp] v0.1.0** (#1):
    - [New] The player's own car heard from outside: `IsPlayerVehicle` read once per car through RTTI; its voice
      goes to the top of the range and `atr_own_car` (3888816778, per playing id) carries the Car Radio slider's level
      (`volume_music_car_radio`, global, through the interior bus's slider curve: silent / -16 / -9 / -5 / 0 dB at
      0 / 25 / 50 / 75 / 100). A Volume curve on the NPC mixer adds it (dB-scaled amplitude-1 points, curve id
      0xA7730001). Verified on the voice: -9.0, -16.0 and -96 dB at 50, 25 and 0.
- **[AudibleTrafficRadios] [OpenAir.cpp, Muffling.cpp, Tuning.cpp] v0.1.0**:
    - [New] Open cars fill more of the street's echo: a curve on `atr_open_air` raises each NPC receiver sound's
      `GameAuxSendVolume` (0x0C) from its own value (-16 most, -12 hyper, -6 police) to -5 dB, the world radio's.
      Added through the node's `SetRTPC`, additive, no scaling, plain dB points.
    - [Fix] The same curve with dB scaling read +2.5 as +765 dB on the voice and silenced the send (reverb bus
      meters fell 5 to 7 dB when open, against 3 dB with no curve).
    - [New] Tuning build: reverb view (`Query::GetGameObjectAuxSendValues` 0x1ad2470 plus bus meters on each
      reverb bus seen) and send view (a hook on `CAkBehavioralCtx::GetAuxSendsValues` 0x1ad66a0 logging the voice
      context's send at +0xb0 and its value block at +0x88).
    - [Verified] Wall occlusion is right as shipped: -12.0 dB and low-pass 57 on the voice indoors.
- **[AudibleTrafficRadios] [Levels.cpp, OpenAir.cpp] v0.1.0**:
    - [Fix] A car that left traffic (kind 3) is keyed on its emitter's `+0x108`, the id `RadioEmitter::GetEntityId`
      returns, which holds the traffic car's `+0x138` entity id (four cars matched in the tuning log). It keeps its
      level and is read for openness. Verified in game: levels unchanged across the switch, openness 0.25 and 0.50.
    - [New] Tuning build: the `level:` line names the listener kind and the emitter's `+0x108`.
- **[AudibleTrafficRadios] [Ducks.cpp] v0.1.0**:
    - [New] Combat and police music no longer duck traffic radios: the duck entries on `Music_Systemic_Combat`
      (2791646749) and `Music_Systemic_Police` (318512183) aimed at `Music_Diagetic_Radios_Vehicle_NPC`
      (194813043) are set from -96 to 0 dB, once, under a tried Wwise lock, guarded on target, property 0 and
      value. The player radio bus's entries on the same buses and the quest ducks are not touched. Verified in
      game (Testing): both lines logged at load, traffic radios heard through a wanted level and a fight.

### [2026-10-06] Session
- **[AudibleTrafficRadios] [Main.cpp] v0.1.0** (open-air cars and occlusion):
    - [Change] The level range tops out at +9 dB (was +12): some cars sounded too loud.
    - [Change] Open-air is graded: only each receiver's treble shelf fades with `atr_open_air` (effect curves on
      `CAkFxBase`, exclusive and absolute), the output evens the loudness and adds 3 dB; the per-object `BypassFX`
      switch and the muscle RoomVerb bypass are gone.
    - [New] Broken glass makes a car open-air: each pane of `vehicle::BaseObject +0x600` -> data -> `+0x298`
      (count `+0x2a4`, stride 0x30) and the windshield `+0x2a8` asked `game::VehicleDestruction::Glass::IsShattered`
      (`0x273094`); the offsets are checked against the bytes of `OnGlassDestruction` and the save code first.
    - [New] Open-air traffic cars: every 250 ms each car with a radio voice is read through RTTI
      (`ScriptGameInstance.FindEntityByID` on the emitter's entity id `+0x138`, `GetVehiclePS`, `GetDoorState` /
      `GetWindowState` for seat doors 0 to 3; `GetRecordID` then the `hasSideWindows` and `player_audio_resource`
      flats once per car). An open car's receiver sound gets its effect slots 0 and 1 bypassed for its Wwise game
      object only through `CAkParameterNodeBase::BypassFX` (`0x1ade2b0`: node, slot, bypass, `CAkRegisteredObj*`
      from `CAkRegistryMgr::GetObj` `0x1b3b230`, fromReset); the lock is tried only when a voice's game object is
      unknown or its car changed state, and entries for gone game objects are swept every 5 s.
    - [New] Occlusion: the world radio's `game_occlusion` curves (volume additive dB 0 to -0.7488, low-pass filter
      0 to 57) attached to the NPC mixer through the parameter node's `SetRTPC` virtual (`0x1adda50`) once,
      beside the volume write.
    - [New] Voices carry their receiver index and whether their key is a car.
    - [New] `ATR_TUNE`: `open 1` treats every car as open-air; a `cars:` line every 10 s.
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
