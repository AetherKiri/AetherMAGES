#!/usr/bin/env python3
"""Drive MO8 through the public host API using a recorded JSON input plan.

Input plans support ticks, key, click, capture and quit. No private engine
memory is changed. Every capture retains native PPM and lossless PNG.
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import struct
import zlib
import os


def save_image(path, rgba, width=1920, height=1080):
    rgb = bytearray()
    for i in range(0, len(rgba), 4):
        rgb.extend(rgba[i:i + 3])
    path.with_suffix(".ppm").write_bytes(f"P6\n{width} {height}\n255\n".encode() + rgb)
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.with_suffix(".png").write_bytes(b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(rows, 1)) + chunk(b"IEND", b""))


class Desc(C.Structure):
    _fields_ = [("struct_size", C.c_uint32), ("api_version", C.c_uint32),
                ("data", C.c_char_p), ("writable", C.c_char_p),
                ("width", C.c_uint32), ("height", C.c_uint32),
                ("log_level", C.c_uint32), ("reserved", C.c_uint64 * 4),
                ("pointers", C.c_void_p * 4)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("runtime", type=Path)
    ap.add_argument("writable", type=Path)
    ap.add_argument("game", type=Path)
    ap.add_argument("output", type=Path)
    ap.add_argument("plan", type=Path)
    ap.add_argument("--interactive", action="store_true", help="Read JSON actions from a named pipe")
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    args.writable.mkdir(parents=True, exist_ok=True)
    lib = C.CDLL(str(args.runtime / "lib/libimpacto_host.so"))
    lib.impacto_host_create.argtypes = [C.POINTER(Desc), C.POINTER(C.c_void_p)]
    lib.impacto_host_open_game.argtypes = [C.c_void_p, C.c_char_p, C.c_char_p, C.c_char_p]
    lib.impacto_host_tick.argtypes = [C.c_void_p, C.c_uint32]
    lib.impacto_host_send_key.argtypes = [C.c_void_p, C.c_uint32, C.c_int32]
    lib.impacto_host_send_pointer.argtypes = [C.c_void_p, C.c_double, C.c_double, C.c_int32, C.c_int32]
    lib.impacto_host_read_frame_rgba.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t]
    lib.impacto_host_destroy.argtypes = [C.c_void_p]
    lib.impacto_host_read_audio_f32.argtypes = [C.c_void_p, C.c_void_p, C.c_int64]
    lib.impacto_host_read_audio_f32.restype = C.c_int64
    desc = Desc(C.sizeof(Desc), 0x01000000, str(args.runtime).encode(),
                str(args.writable).encode(), 1920, 1080, 5)
    host = C.c_void_p()
    def check(result):
        if result != 0:
            raise RuntimeError(f"host result {result}")
    check(lib.impacto_host_create(C.byref(desc), C.byref(host)))
    audio = (C.c_float * 16384)()
    total = 0
    def ticks(count, advance=False):
        nonlocal total
        for i in range(count):
            if advance and i % 10 == 0:
                check(lib.impacto_host_send_key(host, 40, 1))
            if advance and i % 10 == 5:
                check(lib.impacto_host_send_key(host, 40, 0))
            check(lib.impacto_host_tick(host, 16))
            lib.impacto_host_read_audio_f32(host, audio, len(audio))
            total += 1
        if advance:
            check(lib.impacto_host_send_key(host, 40, 0))
    records = []
    def actions():
        if args.interactive:
            with os.fdopen(os.open(args.plan, os.O_RDWR), "r") as pipe:
                for line in pipe:
                    yield json.loads(line)
        else:
            yield from json.loads(args.plan.read_text())
    try:
        check(lib.impacto_host_open_game(host, str(args.game).encode(), b"mo8", str(args.game).encode()))
        for action in actions():
            print(f"save-test tick={total} action={json.dumps(action)}", flush=True)
            records.append({"tick": total, "action": action})
            op = action[0]
            if op == "ticks":
                ticks(action[1], len(action) > 2 and action[2])
            elif op == "key":
                check(lib.impacto_host_send_key(host, action[1], 1))
                ticks(2)
                check(lib.impacto_host_send_key(host, action[1], 0))
                ticks(30)
            elif op == "click":
                x, y = action[1:3]
                button = action[3] if len(action) > 3 else 1
                check(lib.impacto_host_send_pointer(host, x, y, 1, -1))
                ticks(3)
                check(lib.impacto_host_send_pointer(host, x, y, button, 1))
                ticks(2)
                check(lib.impacto_host_send_pointer(host, x, y, button, 0))
                ticks(60)
            elif op == "capture":
                pixels = (C.c_uint8 * (1920 * 1080 * 4))()
                check(lib.impacto_host_read_frame_rgba(host, pixels, len(pixels)))
                save_image(args.output / action[1], bytes(pixels))
            elif op == "quit":
                break
            else:
                raise ValueError(op)
    finally:
        lib.impacto_host_destroy(host)
        (args.output / "inputs.json").write_text(json.dumps(records, indent=2) + "\n")
    print(f"save-test done ticks={total}", flush=True)


if __name__ == "__main__":
    main()
