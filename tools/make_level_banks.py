r"""Dev-only: the seven NPC receiver chains, unchanged, under one copy of the NPC mixer per bank whose volume is
the only difference. One bank per level; events are atr_l<level>_<class>. Never shipped.

Run:  python make_level_banks.py <radio.bnk> <out dir> [levels in dB...]
"""
import os
import struct
import sys

from make_npc_receiver_bank import (NPC_MIXER, RADIO_BANK, BANK_VERSION, LANGUAGE_ID, HIRC_SOUND, HIRC_ACTION,
                                    HIRC_EVENT, HIRC_ACTOR_MIXER, fnv, read_hirc, replace_u32, object_bytes)

CLASSES = ["lowend", "muscle", "sports", "suv", "truck", "hyper", "police"]
VANILLA_MIXER_VOLUME = -8.0


def build(radio_path, out_dir, level):
    radio = read_hirc(radio_path)
    _, mixer_body = radio[NPC_MIXER]
    prefix = f"atr_l{int(level)}"
    bank_id = fnv(prefix + "_bank")
    new_mixer = fnv(prefix + "_mixer")

    sounds, rest, children = [], [], []
    for cls in CLASSES:
        vanilla = f"radio_car_{cls}_npc"
        _, event_body = radio[fnv(vanilla)]
        assert event_body[4] == 1, f"{vanilla} is not a single-action event"
        action_id = struct.unpack_from("<I", event_body, 5)[0]
        _, action_body = radio[action_id]
        sound_id = struct.unpack_from("<I", action_body, 6)[0]
        _, sound_body = radio[sound_id]
        name = f"{prefix}_{cls}"
        ns, na = fnv(name + "_sound"), fnv(name + "_action")
        sound = bytearray(sound_body)
        struct.pack_into("<I", sound, 0, ns)
        assert replace_u32(sound, NPC_MIXER, new_mixer) == 1, f"{name}: sound parent"
        action = bytearray(action_body)
        struct.pack_into("<I", action, 0, na)
        assert replace_u32(action, sound_id, ns) == 1
        assert replace_u32(action, RADIO_BANK, bank_id) == 1
        event = bytearray(event_body)
        struct.pack_into("<I", event, 0, fnv(name))
        struct.pack_into("<I", event, 5, na)
        sounds.append((HIRC_SOUND, sound))
        rest += [(HIRC_ACTION, action), (HIRC_EVENT, event)]
        children.append(ns)

    assert struct.unpack_from("<I", mixer_body, len(mixer_body) - 32)[0] == 7, "the NPC mixer's child list moved"
    mixer = bytearray(mixer_body[:len(mixer_body) - 32])
    vanilla_volume = struct.pack("<f", VANILLA_MIXER_VOLUME)
    assert mixer.count(vanilla_volume) == 1, "the mixer's -8 dB volume is not one unique value"
    at = mixer.index(vanilla_volume)
    struct.pack_into("<f", mixer, at, float(level))
    struct.pack_into("<I", mixer, 0, new_mixer)
    mixer += struct.pack("<I", len(children)) + b"".join(struct.pack("<I", c) for c in sorted(children))

    # sounds before the mixer that lists them: a mixer resolves its children as it loads
    objects = sounds + [(HIRC_ACTOR_MIXER, mixer)] + rest
    hirc = struct.pack("<I", len(objects)) + b"".join(object_bytes(t, b) for t, b in objects)
    bkhd = struct.pack("<IIIIII", BANK_VERSION, bank_id, LANGUAGE_ID, 16, 476, 0)
    bkhd += struct.pack("<IIII", bank_id, 1, 0, 0)
    data = b"BKHD" + struct.pack("<I", len(bkhd)) + bkhd + b"HIRC" + struct.pack("<I", len(hirc)) + hirc
    path = os.path.join(out_dir, prefix + ".bnk")
    open(path, "wb").write(data)
    print(f"{path}: {len(data)} bytes, mixer volume {level:+.0f} dB")


if __name__ == "__main__":
    radio_path, out_dir = sys.argv[1:3]
    for level in (sys.argv[3:] or ["0", "6", "12"]):
        build(radio_path, out_dir, float(level))
