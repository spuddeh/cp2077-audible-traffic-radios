r"""Dev-only: one bank per variant of the lowend NPC receiver chain, to find the link that silences it.

Each variant is the exact clone (Event, Play action, CAkSound, a copy of the NPC mixer) under ids from its
own name, with one link changed. Cuts are made at field offsets read from wwiser's dump of radio.bnk, so
a variant changes those bytes and nothing else. These banks are never shipped.

Run:  python make_variant_banks.py <radio.bnk> <radio.bnk.xml> <out dir>
"""
import os
import struct
import sys
import xml.etree.ElementTree as ET

from make_npc_receiver_bank import (NPC_MIXER, NPC_BUS, NPC_ATTENUATION, WORLD_BUS, WORLD_ATTENUATION,
                                    RADIO_BANK, BANK_VERSION, LANGUAGE_ID, HIRC_SOUND, HIRC_ACTION,
                                    HIRC_EVENT, HIRC_ACTOR_MIXER, fnv, read_hirc, replace_u32, object_bytes)

VANILLA = "radio_car_lowend_npc"


def field_offsets(xml_path, object_id):
    """Absolute file offsets of every field in one HIRC object, by name (first occurrence)."""
    for obj in ET.parse(xml_path).getroot().iter("object"):
        uid = obj.find("./field[@name='ulID']")
        if uid is None or uid.get("value") != str(object_id):
            continue
        out = {}
        for f in obj.iter("field"):
            if f.get("offset") is not None:
                out.setdefault(f.get("name"), int(f.get("offset")))
        start = out["eHircType"]
        return {k: v - start - 5 for k, v in out.items()}  # body-relative
    raise KeyError(object_id)


def cut(body, start, end, insert=b""):
    return bytearray(body[:start]) + insert + bytearray(body[end:])


def build(radio_path, xml_path, out_dir):
    radio = read_hirc(radio_path)
    _, event_body = radio[fnv(VANILLA)]
    action_id = struct.unpack_from("<I", event_body, 5)[0]
    _, action_body = radio[action_id]
    sound_id = struct.unpack_from("<I", action_body, 6)[0]
    _, sound_body = radio[sound_id]
    _, mixer_body = radio[NPC_MIXER]
    sound_at = field_offsets(xml_path, sound_id)
    action_at = field_offsets(xml_path, action_id)
    mixer_at = field_offsets(xml_path, NPC_MIXER)

    def no_eq(sound, action, mixer):
        # NodeInitialFxParams: bIsOverrideParentFX, uNumFx, then the FX chunks, up to the metadata block
        return cut(sound, sound_at["bIsOverrideParentFX"], sound_at["bIsOverrideParentMetadata"], b"\x00\x00"), action, mixer

    def no_fade(sound, action, mixer):
        # the first prop bundle holds one prop (TransitionTime 250): cProps, pID, pValue -> cProps 0
        return sound, cut(action, action_at["cProps"], action_at["cProps"] + 1 + 1 + 4, b"\x00"), mixer

    def no_rtpc(sound, action, mixer):
        # InitialRTPC: uNumCurves (u16) and its curves, up to the child list
        return sound, action, cut(mixer, mixer_at["uNumCurves"], mixer_at["ulNumChilds"], b"\x00\x00")

    def world_bus(sound, action, mixer):
        mixer = bytearray(mixer)
        assert replace_u32(mixer, NPC_BUS, WORLD_BUS) == 1
        return sound, action, mixer

    def world_attenuation(sound, action, mixer):
        mixer = bytearray(mixer)
        assert replace_u32(mixer, NPC_ATTENUATION, WORLD_ATTENUATION) == 1
        return sound, action, mixer

    # The lowend sound's one insert, a Parametric EQ. Its body: ulID, fxID, uSize, then three bands of
    # eFilterType u32, fGain, fFrequency, fQFactor f32, bOnOff u8 (17 bytes each) from offset 12.
    eq_id = 828314749
    _, eq_body = radio[eq_id]
    band = lambda i: 12 + 17 * i

    def eq_copy(edit):
        def change(sound, action, mixer, name):
            eq = bytearray(eq_body)
            edit(eq)
            new_eq = fnv(name + "_eq")
            struct.pack_into("<I", eq, 0, new_eq)
            assert replace_u32(sound, eq_id, new_eq) == 1, f"{name}: sound cites its EQ other than once"
            return sound, action, mixer, [(17, eq)]
        return change

    def peaking(eq):
        assert struct.unpack_from("<I", eq, band(1))[0] == 3, "band 2 is no longer a notch"
        struct.pack_into("<I", eq, band(1), 6)

    def band_off(i):
        def edit(eq):
            eq[band(i) + 16] = 0
        return edit

    def shelf(freq=None, gain=None, q=None):
        def edit(eq):
            assert struct.unpack_from("<I", eq, band(2))[0] == 5, "band 3 is no longer a high shelf"
            if gain is not None:
                struct.pack_into("<f", eq, band(2) + 4, gain)
            if freq is not None:
                struct.pack_into("<f", eq, band(2) + 8, freq)
            if q is not None:
                struct.pack_into("<f", eq, band(2) + 12, q)
        return edit

    sweep = {f"atr_s{int(f)}": eq_copy(shelf(freq=f)) for f in (200.0, 500.0, 1000.0, 3000.0, 6000.0, 12000.0)}
    sweep["atr_s1500_q0707"] = eq_copy(shelf(q=0.707))
    sweep["atr_s1500_g12"] = eq_copy(shelf(gain=-12.0))

    eq_variants = {
        **sweep,
        "atr_v8_eq_copy": eq_copy(lambda eq: None),
        "atr_v9_eq_peaking": eq_copy(peaking),
        "atr_v10_eq_band2_off": eq_copy(band_off(1)),
        "atr_v11_eq_band3_off": eq_copy(band_off(2)),
    }

    variants = {
        "atr_v3_no_eq": no_eq,
        "atr_v4_no_fade": no_fade,
        "atr_v5_no_mixer_rtpc": no_rtpc,
        "atr_v6_world_bus": world_bus,
        "atr_v7_world_attenuation": world_attenuation,
    }
    for name, change in list(variants.items()) + list(eq_variants.items()):
        extra = []
        if name in eq_variants:
            sound, action, mixer, extra = change(bytearray(sound_body), bytearray(action_body),
                                                 bytearray(mixer_body), name)
        else:
            sound, action, mixer = change(bytearray(sound_body), bytearray(action_body), bytearray(mixer_body))
        bank_id = fnv(name + "_bank")
        ns, na, nm = fnv(name + "_sound"), fnv(name + "_action"), fnv(name + "_mixer")
        assert struct.unpack_from("<I", mixer, len(mixer) - 32)[0] == 7, f"{name}: child list moved"
        mixer = mixer[:len(mixer) - 32] + struct.pack("<II", 1, ns)
        struct.pack_into("<I", mixer, 0, nm)
        struct.pack_into("<I", sound, 0, ns)
        assert replace_u32(sound, NPC_MIXER, nm) == 1, f"{name}: sound parent"
        struct.pack_into("<I", action, 0, na)
        assert replace_u32(action, sound_id, ns) == 1, f"{name}: action target"
        assert replace_u32(action, RADIO_BANK, bank_id) == 1, f"{name}: action bank"
        event = bytearray(event_body)
        struct.pack_into("<I", event, 0, fnv(name))
        struct.pack_into("<I", event, 5, na)
        objects = extra + [(HIRC_SOUND, sound), (HIRC_ACTOR_MIXER, mixer), (HIRC_ACTION, action), (HIRC_EVENT, event)]
        hirc = struct.pack("<I", len(objects)) + b"".join(object_bytes(t, b) for t, b in objects)
        bkhd = struct.pack("<IIIIII", BANK_VERSION, bank_id, LANGUAGE_ID, 16, 476, 0)
        bkhd += struct.pack("<IIII", bank_id, 1, 0, 0)
        data = b"BKHD" + struct.pack("<I", len(bkhd)) + bkhd + b"HIRC" + struct.pack("<I", len(hirc)) + hirc
        path = os.path.join(out_dir, name + ".bnk")
        open(path, "wb").write(data)
        print(f"{path}: {len(data)} bytes, event {fnv(name)}")


if __name__ == "__main__":
    build(*sys.argv[1:4])
