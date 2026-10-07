# How Audible Traffic Radios works

A technical write-up for modders: how Cyberpunk 2077 decides whether a traffic car's radio is heard, what this
plugin changes, and the Wwise techniques it uses, which apply well beyond radios. Addresses are RVAs in the
Cyberpunk 2077 2.31 executable. Every one is checked byte for byte before use; on any other build the plugin logs
a line and changes nothing.

## 1. Why traffic radios are not heard in vanilla

A traffic car does tune its radio. Two things stop it being heard.

### The station gate

Each radio station is a listener list: every receiver tuned to it (world radios, ambient street music, the
player's car and pocket radio, traffic cars) adds itself, with a **kind** byte at `+0x12c`:

| Kind | Receiver |
| --- | --- |
| 1 | world radio device |
| 2 | traffic car |
| 3 | a car that has left traffic (hit, fleeing, abandoned) |
| 4 | the player's receivers (car, pocket radio) |
| 5 | ambient street music spot |

`RadioStation::PostSoundUpdate` (`0x2a759c`) sets a station active from a predicate (`0x2a775c`, RED4ext hash
`2590772874`) that reads the radio system's **mode** (`+0x27b`):

- **Mode 0:** a station with a counting listener plays.
- **Mode 1:** a station whose every listener is kind 2 stays silent (`cmp byte [rcx+0x12c], 2` at `0x2a782b`).
  So a station only traffic is tuned to never plays.
- **Mode 2** (the player is in a car with its radio on): a station plays only if a player receiver is tuned to it
  (`cmp byte [station+0x21e], dil; sete al` in the cold block `0x1e5144e`, reached through the predicate's own
  `jne` at `+0xab`). Everything else is silent: traffic, world radios, street music.

### The mix

Every traffic receiver (`radio_car_<class>_npc`, seven classes) sits under one Wwise actor mixer, `398448775`,
on the bus `Music_Diagetic_Radios_Vehicle_NPC`. The mixer is at -8 dB, each receiver a further -2 to -6 dB, and
its RTPC `veh_engage_moving_faster` takes another 12 dB off unless it is 1, which the game only sets for the
player's car. Each receiver is voiced as music through a closed car body (bass-heavy EQ, a cabin reverb on some)
and fades out by 35 m. Together that leaves a traffic radio 20 to 30 dB under a world radio at the same distance.

## 2. What the plugin changes

### Traffic-only stations play

One byte: the mode 1 compare's immediate (`0x2a7831`) becomes `0xFF`, which no listener kind matches.

### Other radios play while the player's car radio is on

Three bytes: the mode 2 branch's `sete al` (`0F 94 C0`) becomes `xor al, al; nop` (`32 C0 90`), so mode 2 plays
like mode 0. The cold block is found through the predicate's `jne`, not by address.

### A level per car

The NPC mixer is set to the top of a range (+9 dB) and each traffic radio voice is lowered by its own share of
it (0 to 9 dB):

- **Per voice, not per game object.** `veh_engage_moving_faster` is set through
  `AK::SoundEngine::SetRTPCValueByPlayingID` (`0x1acf6a0`; it looks up the voice's game object at `0x1ae8dc0` and
  returns `0x5E` AK_PlayingIDNotFound or `0x66` AK_NotInitialized). A car's engine sounds share its game object,
  so a game-object-wide value would change them too.
- **Bypassing the parameter's smoothing.** The parameter has its own slew, built for a car's speed; without the
  bypass flag a value glides in over about 3 s and every restarted voice swells up from 12 dB under.
- **Keyed on the car.** A car's radio voice restarts often while in earshot (station slot changes, the 35 m edge):
  65 of 132 cars in one session, 38 of those within 25 m. A `TrafficVehicleEmitter` (vtable `0x2b458a0`) keeps
  its car's entity id at `+0x138`; the level is a hash of that, so a car comes back at the same level. A kind 3
  emitter (a plain `RadioEmitter`) keeps the same id at `+0x108`, the field `RadioEmitter::GetEntityId`
  (`0x9dac30`) returns, so a car keeps its level and is read for openness after it leaves traffic. A traffic
  emitter's `+0x108` is a different id.
- **Weighted to the middle.** The share is the mean of three independent hashes, so most cars sit near +6 dB and
  few at either end.
- **Finding the voices.** Every frame, every listener of every station (engine root, hash `2549221846`, `+0xa8`
  audio system, `+0xe0` radio manager, stations at `+0x0`, count `+0xc`; listeners at station `+0x100`, count
  `+0x10c`). A kind 2 or 3 listener's broadcast event (`+0x120`) is matched by name in its sounds (`+0x58`, count
  `+0x64`; name `+0x8`, playing id `+0x44`, state `+0x59`, 3 = playing). Measured cost: about 1 µs per frame.

### Muffled from inside a car

The game's own `veh_interior` (Wwise game parameter `290459857`, global) is 1 in first person in a car, 0 in
third person or on foot, and lowered by broken glass. Vanilla uses it to take traffic noise down 4 dB with a
low-pass of 25, but never touches radios. The plugin applies the same to the NPC mixer: volume
`top + -4 dB x veh_interior`, low-pass `25 x veh_interior` (the actor-mixer low-pass is property id 2).

### Muffled by walls

The NPC receivers and their mixer read no occlusion parameter, so walls never stopped them; the world radio
receiver (`radio_default_int`) reads `game_occlusion`. The plugin gives the NPC mixer the world radio's own two
curves on `game_occlusion` (volume 0 to -12 dB, low-pass 0 to 57), attached once at load (section 3).

### Open-air cars

Each traffic car with a radio gets an **openness** from 0 to 1, read every 250 ms through RTTI: 0.25 per window
down (`GetWindowState`), 0.5 per seat door open and 0.75 per door torn off (`GetDoorState`, seats 0 to 3), 0.25
per shattered pane, and 1 for a car with no side windows (the record's `hasSideWindows` false, or a `targa` /
`cabrio` `player_audio_resource`). The car is reached from the emitter's entity id with
`ScriptGameInstance.FindEntityByID`. Glass has no script-readable state: each pane of the car's
`game::VehicleDestruction` (`vehicle::BaseObject +0x600`, then its data: panes at `+0x298`, count `+0x2a4`, 0x30
bytes each, windshield at `+0x2a8`) is asked `Glass::IsShattered` (`0x273094`), the call the save code uses. The
saved `brokenGlass` mask is only written when the car is saved.

Openness is a game parameter of the plugin's own (`atr_open_air`) set on the car's Wwise game object with a 250 ms
glide. Each receiver's EQ is CDPR's sound for that car class: bass and mid bands for the speakers and cabin, a
treble shelf for the closed body. Only the **treble shelf** fades with openness, to 0 dB at 1, so a car keeps its
class's character. The EQ's output takes off the loudness the lift adds (pink noise, K-weighted: 6.9 dB low-end,
5.9 muscle, 2.4 sports/SUV/truck/hyper, 1.0 police) and adds 3 dB, so every open car type is 3 dB louder than
closed and the balance between types holds.

**The street's echo opens with the car.** Each NPC receiver sound already uses the game-defined aux sends (the
game's own area reverb, the same one world radios use) at its own `GameAuxSendVolume` (property `0x0C`): -16 dB for
most classes, -12 hyper, -6 police, against the world radio's -5. A curve on `atr_open_air` raises it to -5 at full
openness. It is added through the node's `SetRTPC` with **no scaling and its points in plain dB**: with dB scaling
the reading differs by property (a volume curve read -0.7488 as -12 dB, a send curve read +2.5 as +765 dB, which
silences the send). The value a voice really uses is the context's: `CAkBehavioralCtx::GetAuxSendsValues`
(`0x1ad66a0`) reads the send in dB at context `+0xb0` and the volume at `+0x88`; the tuning build hooks it.

### Stations on traffic cars

A traffic car picks from its sound set's `matchingStartupRadioStations` (`audioVehicleMetadata` in
`base\sound\metadata\cooked_metadata.audio_metadata`, 176 sets, none in ep1's). 152 sets share nine stations; Growl
FM, Impulse, Dark Star and Samizdat are on none, and Royal Blue only on Delamain. The plugin edits the loaded lists
once (`ResourceLoader::FindToken`, then the `entries` through RTTI), the way the game themes Delamain and the
Villefort executives: Growl FM on the shared lists, Impulse on the sports and hyper cars, Dark Star on the gang
and nomad variants, Royal Blue on the executive cars and limousines. Samizdat stays off, as world radios skip it.
Six lists spell Morro Rock `radio_station 01_att_rock`, which names no station; they are corrected. A list edited
after load is the one traffic picks from.

### Through fights and a wanted level

`init.bnk` ducks `Music_Diagetic_Radios_Vehicle_NPC` (194813043) by -96 dB while `Music_Systemic_Combat` or
`Music_Systemic_Police` plays, which outweighs the boost. Each ducking bus keeps its duck list at `+0x110` (count
`+0x134`); an entry holds its target bus at `+0x08`, its volume at `+0x0c` and its property at `+0x1c`. The plugin
sets the NPC bus's entry on both to 0 dB, once, under a tried Wwise lock, only over -96. The same buses' entries
on the player radio bus are not touched, so the player's radio follows the game, or RadioXL's switches.

## 3. Editing loaded Wwise objects in memory

No sound bank is shipped. The mixer is changed on the object Wwise already loaded:

- **The index.** `g_pIndex` (`0x339f7b8`) holds one hash table per object type; audio nodes are table 0, buckets at
  `+0x40`, count at `+0x48`. A node sits in bucket `id % count`, chained through `+0x8`, its id at `+0x10`.
- **The pointer is a sub-object.** The index hands back the `CAkPBIAware` part, 0x10 into the object. The object
  start has the parameter-node vtable (`0x2ee0798`) at `+0x08` - checked before any write.
- **The property bundle** is at object `+0x88`: a count byte, that many property ids, then the values from the
  next 4-byte boundary.
- **The setter.** `CAkParameterNode::SetAkProp(object, prop, value, min, max)` (`0x1b07990`), Wwise's own
  live-editing setter, called on the object start. The plugin writes only over the value it expects (the vanilla
  -8 dB, or its own last write).

### Giving a loaded object a game parameter it never had

A bank load attaches each RTPC curve through the object's own setter, and those setters still work after load:

- **Nodes** (sounds, actor mixers, buses): the parameter node's `SetRTPC` virtual (vtable slot `0x1c0`,
  `0x1adda50`): `(object start, desc, points)`, 1 on success. `desc` is `{u8 type, u8 accumulation, u8 scaling,
  pad, u32 rtpc, u32 parameter, u32 curve id, u32 count}`, a point `{float from, float to, u32 interpolation}`.
  Copy type, accumulation and scaling from a vanilla curve doing the same job; a volume point with dB scaling is
  stored as amplitude - 1 (`-0.7488` is -12 dB). This is how the walls work.
- **Effects:** `CAkFxBase`'s own `SetRTPC` (`0x1b50b20`), same arguments, which also reaches effect instances
  already playing. Effects are `g_pIndex` table 9, the indexed pointer being the object (`CAkFxCustom` vtable
  `0x2f75ac0`). Two traps, both measured with a hook on the Parametric EQ's `SetParam` (`0x1ab4640`): **the curve's
  value replaces the parameter** rather than adding to it, and **a curve with dB scaling is converted again** on
  the way. Write absolute values, exclusive accumulation, no scaling; then the parameter receives the curve value as
  is. A curve's points blend in their stored form, so five or more keep a dB fade even.
- **Parametric EQ parameter ids** (from `SetParam`'s switch): band `n` (0 to 2) is `n*5` + 0 type, 1 gain (dB,
  clamped to ±24), 2 frequency, 3 Q, 4 on/off; 15 is the output gain.

### Never wait on Wwise's lock from the game thread

Wwise's global lock is a `CRITICAL_SECTION` (`0x339ffd0`, found from the `lea` at `+0x17` of
`CAkFunctionCritical`'s enter, `0x1af6d90`) that the audio thread holds for its whole render pass. Any query or
property write enters it. Called from the game thread every frame, a blocking call measured 30 to 180 µs on
average with frames stalled for up to 28 ms. The plugin calls `TryEnterCriticalSection` and skips the frame when
the audio thread has the lock; the section is re-entrant, so Wwise's own enter inside the call goes straight
through.

## 4. Measuring, not guessing

Every level in this mod was set with meters, not by ear alone: quiet sounds under city noise read as silent and
produced one wrong conclusion during development.

- **Bus meters.** `AK::SoundEngine::RegisterBusMeteringCallback` (`0x1acb900`) gives peak and RMS per channel. A bus
  with no effect never calls back for 3D voices, so a meter goes on the nearest ancestor with one:
  `Music_Diagetic_RTPC` (NPC cars and world radios), `Music_Systemic` (combat, police and open-world music),
  `Music_Radio_Car_Player_DVR` (the player's radio). Wwise keeps one callback per bus; the engine's own meters sit
  on `Music_Systemic_Combat`, `_Police`, `Music_Diagetic_Radios_Vehicle_NPC` and `VO_Important_Somi_Holo`, and
  those three effect-less buses are why the engine's radio meters read 0.
- **The tuning build** (CMake option `ATR_TUNE`) reads `atr_tune.txt` beside the DLL every 2 s
  (`levels <bottom> <top>`, `muffle <dB> <low-pass>`, `open 1` for every car fully open, `weights <window> <door>
  <torn-off> <pane>`), logs its per-frame cost by part every 10 s, each car's level and openness, and every gain,
  Q and output the Parametric EQs receive. It is never released. Measured cost of the release features: 1.6 to 6
  µs per frame with radios playing.

## 5. What the game does that this plugin leaves alone

- The quest-driven ducks on the NPC radio bus (`Music_Quest_Muting_Radios_Guns_DVR`) stay the game's.
- Important and gameplay dialogue are set to lower the radio buses through RTPC sidechains (read from `init.bnk`,
  not measured). A phone call does not lower traffic radios (heard).
- Police traffic cars never start a radio; the police scanner station is not heard from traffic.
- A hijacked car hands over to the player's receiver within 0.3 s, on the same station for vanilla stations.
