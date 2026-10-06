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

The NPC mixer is set to the top of a range (+12 dB) and each traffic radio voice is lowered by its own share of
it (0 to 12 dB):

- **Per voice, not per game object.** `veh_engage_moving_faster` is set through
  `AK::SoundEngine::SetRTPCValueByPlayingID` (`0x1acf6a0`; it looks up the voice's game object at `0x1ae8dc0` and
  returns `0x5E` AK_PlayingIDNotFound or `0x66` AK_NotInitialized). A car's engine sounds share its game object,
  so a game-object-wide value would change them too.
- **Bypassing the parameter's smoothing.** The parameter has its own slew, built for a car's speed; without the
  bypass flag a value glides in over about 3 s and every restarted voice swells up from 12 dB under.
- **Keyed on the car.** A car's radio voice restarts often while in earshot (station slot changes, the 35 m edge):
  65 of 132 cars in one session, 38 of those within 25 m. A `TrafficVehicleEmitter` (vtable `0x2b458a0`) keeps
  its car's entity id at `+0x138`; the level is a hash of that, so a car comes back at the same level. Kind 3
  emitters have no such field and fall back to the voice's playing id.
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
  (`levels <bottom> <top>`, `muffle <dB> <low-pass>`) and logs its own per-frame cost every 10 s. It is never
  released.

## 5. What the game does that this plugin leaves alone

- Combat and police music silence the NPC radio bus (`init.bnk` ducks it by -96 dB), which outweighs the boost.
- Important and gameplay dialogue lower the radio buses through RTPC sidechains.
- Police traffic cars never start a radio; the police scanner station is not heard from traffic.
- A hijacked car hands over to the player's receiver within 0.3 s, on the same station for vanilla stations.
