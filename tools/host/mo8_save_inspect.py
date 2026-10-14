#!/usr/bin/env python3
"""Validate the MO8 host sidecar and report saved checkpoints independently."""
import argparse
import json
import struct
import zlib
from pathlib import Path


def inspect(path):
    data = path.read_bytes()
    payload = data[24:]
    magic, version, size, checksum, reserved = struct.unpack("<8sIIII", data[:24])
    assert magic == b"MO8HOST1" and version == 1 and reserved == 0
    slot_size = 0x42A8 + 240 * 135 * 2
    assert len(payload) == size == 48 + slot_size * 96
    assert zlib.crc32(payload) == checksum
    assert sorted(payload[:48]) == list(range(48))
    entries = []
    for i in range(96):
        p = payload[48 + i * slot_size:48 + (i + 1) * slot_size]
        if not p[0]:
            continue
        words = struct.unpack("<" + "H" * ((0x42A8 - 6) // 2), p[6:0x42A8])
        expected_sum = (sum(words) + 0x4716) & 0xFFFF
        expected_xor = 0x1482
        for word in words:
            expected_xor ^= word
        assert struct.unpack_from("<HH", p, 2) == (expected_sum, expected_xor)
        group, checkpoint, stack, buffer, page = (
            struct.unpack_from("<I", p, at)[0]
            for at in (0x395C, 0x3968, 0x3974, 0x39BC, 0x3A00))
        assert p[0] == 1 and group in (5, 6, 7) and stack <= 8 and buffer < 16 and page < 3
        entries.append({"type": "full" if i < 48 else "quick", "physical_slot": i % 48,
                        "title": struct.unpack_from("<I", p, 0x14)[0],
                        "group": group, "checkpoint": checkpoint, "buffer": buffer,
                        "page": page, "call_depth": stack,
                        "thumbnail_crc": f"{zlib.crc32(p[0x42A8:]):08x}",
                        "thumbnail_nonzero": sum(b != 0 for b in p[0x42A8:])})
    return {"path": str(path), "bytes": len(data), "crc": f"{checksum:08x}",
            "entries": entries}


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("save", type=Path)
    args = ap.parse_args()
    print(json.dumps(inspect(args.save), indent=2))
