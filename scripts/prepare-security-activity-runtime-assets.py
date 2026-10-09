#!/usr/bin/env python3
"""Generate bounded Aurora Security Activity runtime PNG assets.

Source artwork stays lossless/high-resolution in the repository.  This tool
creates deterministic, 8-bit RGBA, non-interlaced runtime copies using only
the Python standard library so CI does not depend on Pillow/ImageMagick.
"""

import argparse
import binascii
import struct
import zlib
from pathlib import Path

PNG_SIG = b"\x89PNG\r\n\x1a\n"

TARGETS = {
    "aurora_security_activity_panel.png": (800, 500),
    "aurora_security_activity_event_card.png": (700, 120),
    "aurora_security_activity_status_info.png": (128, 128),
    "aurora_security_activity_status_warning.png": (128, 128),
    "aurora_security_activity_status_critical.png": (128, 128),
    "aurora_security_activity_category_auth.png": (128, 128),
    "aurora_security_activity_category_session.png": (128, 128),
    "aurora_security_activity_category_credential.png": (128, 128),
    "aurora_security_activity_header.png": (700, 80),
    "aurora_security_activity_empty.png": (256, 256),
    "aurora_security_activity_error.png": (256, 256),
    "aurora_security_activity_loading.png": (256, 256),
    "aurora_security_activity_load_more.png": (520, 90),
    "aurora_security_activity_filter_button.png": (300, 80),
    "aurora_security_activity_filter_active.png": (128, 128),
    "aurora_security_activity_end.png": (128, 128),
}

def paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c

def read_rgba(path: Path):
    data = path.read_bytes()
    if not data.startswith(PNG_SIG):
        raise ValueError(f"{path.name}: invalid PNG signature")
    pos = len(PNG_SIG)
    width = height = None
    compressed = bytearray()
    while pos + 12 <= len(data):
        length = struct.unpack(">I", data[pos:pos+4])[0]
        kind = data[pos+4:pos+8]
        payload = data[pos+8:pos+8+length]
        crc = struct.unpack(">I", data[pos+8+length:pos+12+length])[0]
        if binascii.crc32(kind + payload) & 0xFFFFFFFF != crc:
            raise ValueError(f"{path.name}: PNG CRC mismatch")
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, ctype, comp, filt, interlace = struct.unpack(
                ">IIBBBBB", payload)
            if depth != 8 or ctype != 6 or comp != 0 or filt != 0 or interlace != 0:
                raise ValueError(
                    f"{path.name}: requires 8-bit RGBA non-interlaced PNG")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
    if width is None or height is None:
        raise ValueError(f"{path.name}: missing IHDR")
    raw = zlib.decompress(bytes(compressed))
    stride = width * 4
    expected = (stride + 1) * height
    if len(raw) != expected:
        raise ValueError(f"{path.name}: unexpected inflated size")
    pixels = bytearray(stride * height)
    for y in range(height):
        src = raw[y * (stride + 1):(y + 1) * (stride + 1)]
        mode = src[0]
        if mode > 4:
            raise ValueError(f"{path.name}: unsupported PNG filter {mode}")
        for x in range(stride):
            value = src[x + 1]
            left = pixels[y * stride + x - 4] if x >= 4 else 0
            up = pixels[(y - 1) * stride + x] if y else 0
            ul = pixels[(y - 1) * stride + x - 4] if y and x >= 4 else 0
            if mode == 1:
                value = (value + left) & 0xFF
            elif mode == 2:
                value = (value + up) & 0xFF
            elif mode == 3:
                value = (value + ((left + up) // 2)) & 0xFF
            elif mode == 4:
                value = (value + paeth(left, up, ul)) & 0xFF
            pixels[y * stride + x] = value
    return width, height, pixels

def resize_bilinear(sw, sh, src, dw, dh):
    if sw == dw and sh == dh:
        return bytes(src)
    dst = bytearray(dw * dh * 4)
    for y in range(dh):
        sy_fp = 0 if dh == 1 else (y * (sh - 1) * 65536) // (dh - 1)
        y0 = sy_fp >> 16
        y1 = min(y0 + 1, sh - 1)
        fy = sy_fp & 0xFFFF
        iy = 65536 - fy
        for x in range(dw):
            sx_fp = 0 if dw == 1 else (x * (sw - 1) * 65536) // (dw - 1)
            x0 = sx_fp >> 16
            x1 = min(x0 + 1, sw - 1)
            fx = sx_fp & 0xFFFF
            ix = 65536 - fx
            for c in range(4):
                p00 = src[(y0 * sw + x0) * 4 + c]
                p10 = src[(y0 * sw + x1) * 4 + c]
                p01 = src[(y1 * sw + x0) * 4 + c]
                p11 = src[(y1 * sw + x1) * 4 + c]
                top = (p00 * ix + p10 * fx) >> 16
                bot = (p01 * ix + p11 * fx) >> 16
                dst[(y * dw + x) * 4 + c] = (top * iy + bot * fy) >> 16
    return bytes(dst)

def chunk(kind: bytes, payload: bytes) -> bytes:
    crc = binascii.crc32(kind + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)

def write_rgba(path: Path, width: int, height: int, pixels: bytes):
    rows = bytearray()
    stride = width * 4
    for y in range(height):
        rows.append(0)
        rows.extend(pixels[y * stride:(y + 1) * stride])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    payload = zlib.compress(bytes(rows), 9)
    path.write_bytes(
        PNG_SIG + chunk(b"IHDR", ihdr) + chunk(b"IDAT", payload) +
        chunk(b"IEND", b""))

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()
    source = Path(args.source_dir)
    output = Path(args.output_dir)
    output.mkdir(parents=True, exist_ok=True)

    for name, (width, height) in TARGETS.items():
        sw, sh, pixels = read_rgba(source / name)
        resized = resize_bilinear(sw, sh, pixels, width, height)
        write_rgba(output / name, width, height, resized)
        print(f"{name}: {sw}x{sh} -> {width}x{height}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
