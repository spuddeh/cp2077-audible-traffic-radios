r"""Build the bank of cloned NPC car radio receivers.

Each vanilla `radio_car_<class>_npc` event plays one CAkSound under the actor mixer 398448775, which
routes to the bus `Music_Diagetic_Radios_Vehicle_NPC`. This clones a receiver's Event, Play action and
CAkSound, plus a copy of that mixer, under new ids from `atr_`-prefixed names. Everything else - the
receive plugin, the insert effects, the attenuation, the bus, the mixer's parent - is cited from the
game's own banks. A variant changes exactly one cited id on its mixer copy.

The plugin points a vanilla event's metadata row at a clone's event id, so a car keeps its vanilla
receiver name and plays the clone.

Run:  python make_npc_receiver_bank.py <radio.bnk> <out.bnk>
"""
import struct
import sys

NPC_MIXER = 398448775            # actor mixer of every radio_car_*_npc sound
NPC_BUS = 194813043              # Music_Diagetic_Radios_Vehicle_NPC
NPC_ATTENUATION = 751971592
WORLD_BUS = 918052088            # Music_Diagetic_Radios_Default, radio_default_int's bus
WORLD_ATTENUATION = 376434992    # radio_default_int's attenuation
RADIO_BANK = 2548238350          # radio.bnk's id, cited by the vanilla Play actions

BANK_NAME = "atr_npc_receivers"
BANK_VERSION = 150
LANGUAGE_ID = 393239870

HIRC_SOUND = 2
HIRC_ACTION = 3
HIRC_EVENT = 4
HIRC_ACTOR_MIXER = 7

# (vanilla event, clone name, {cited id on the mixer copy: replacement})
VARIANTS = [
    ("radio_car_lowend_npc", "atr_v0_exact", {}),
    ("radio_car_suv_npc", "atr_v1_world_bus", {NPC_BUS: WORLD_BUS}),
    ("radio_car_sports_npc", "atr_v2_world_attenuation", {NPC_ATTENUATION: WORLD_ATTENUATION}),
]


def fnv(name):
    h = 2166136261
    for b in name.lower().encode():
        h = (h * 16777619) & 0xFFFFFFFF
        h ^= b
    return h


def read_hirc(path):
    data = open(path, "rb").read()
    offset = 0
    while offset < len(data) - 8:
        if data[offset:offset + 4] == b"HIRC":
            break
        offset += 8 + struct.unpack_from("<I", data, offset + 4)[0]
    pos = offset + 8
    count = struct.unpack_from("<I", data, pos)[0]
    pos += 4
    objects = {}
    for _ in range(count):
        size = struct.unpack_from("<I", data, pos + 1)[0]
        obj_id = struct.unpack_from("<I", data, pos + 5)[0]
        objects[obj_id] = (data[pos], data[pos + 5:pos + 5 + size])
        pos += 5 + size
    return objects


def replace_u32(buf, old, new):
    hits = 0
    for i in range(len(buf) - 3):
        if struct.unpack_from("<I", buf, i)[0] == old:
            struct.pack_into("<I", buf, i, new)
            hits += 1
    return hits


def object_bytes(obj_type, body):
    return bytes([obj_type]) + struct.pack("<I", len(body)) + bytes(body)


def build(radio_path):
    radio = read_hirc(radio_path)
    bank_id = fnv(BANK_NAME)
    mixer_type, mixer_body = radio[NPC_MIXER]
    assert mixer_type == HIRC_ACTOR_MIXER, "398448775 is no longer an actor mixer"
    children = struct.unpack_from("<I", mixer_body, len(mixer_body) - 32)[0]
    assert children == 7, "the NPC mixer's child list has changed"

    hirc = b""
    count = 0
    ids = []
    for vanilla, clone, swaps in VARIANTS:
        event_type, event_body = radio[fnv(vanilla)]
        assert event_type == HIRC_EVENT and event_body[4] == 1, f"{vanilla} is not a single-action event"
        action_type, action_body = radio[struct.unpack_from("<I", event_body, 5)[0]]
        assert action_type == HIRC_ACTION
        sound_id = struct.unpack_from("<I", action_body, 6)[0]
        sound_type, sound_body = radio[sound_id]
        assert sound_type == HIRC_SOUND

        new_event, new_action = fnv(clone), fnv(clone + "_action")
        new_sound, new_mixer = fnv(clone + "_sound"), fnv(clone + "_mixer")

        mixer = bytearray(mixer_body[:len(mixer_body) - 32]) + struct.pack("<II", 1, new_sound)
        struct.pack_into("<I", mixer, 0, new_mixer)
        for old, new in swaps.items():
            assert replace_u32(mixer, old, new) == 1, f"{clone}: mixer cites {old} other than once"

        sound = bytearray(sound_body)
        struct.pack_into("<I", sound, 0, new_sound)
        assert replace_u32(sound, NPC_MIXER, new_mixer) == 1, f"{clone}: sound cites its mixer other than once"

        action = bytearray(action_body)
        struct.pack_into("<I", action, 0, new_action)
        assert replace_u32(action, sound_id, new_sound) == 1
        assert replace_u32(action, RADIO_BANK, bank_id) == 1

        event = bytearray(event_body)
        struct.pack_into("<I", event, 0, new_event)
        struct.pack_into("<I", event, 5, new_action)

        # A mixer resolves its child list as it loads, so the sound precedes it: mixer first is AK_IDNotFound.
        hirc += object_bytes(HIRC_SOUND, sound) + object_bytes(HIRC_ACTOR_MIXER, mixer)
        hirc += object_bytes(HIRC_ACTION, action) + object_bytes(HIRC_EVENT, event)
        count += 4
        ids.append((vanilla, fnv(vanilla), clone, new_event))

    hirc = struct.pack("<I", count) + hirc
    bkhd = struct.pack("<IIIIII", BANK_VERSION, bank_id, LANGUAGE_ID, 16, 476, 0)
    bkhd += struct.pack("<IIII", bank_id, 1, 0, 0)
    data = b"BKHD" + struct.pack("<I", len(bkhd)) + bkhd
    data += b"HIRC" + struct.pack("<I", len(hirc)) + hirc
    return data, bank_id, ids


if __name__ == "__main__":
    radio_path, out_path = sys.argv[1:3]
    data, bank_id, ids = build(radio_path)
    open(out_path, "wb").write(data)
    print(f"{out_path}: {len(data)} bytes, bank {bank_id}")
    for vanilla, vanilla_id, clone, clone_id in ids:
        print(f"  {vanilla} ({vanilla_id}) -> {clone} ({clone_id})")
