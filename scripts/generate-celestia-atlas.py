#!/usr/bin/env python3
"""Rasterize verified Aurora Celestia UI 1.0 TTFs into a bounded kernel atlas.

Host-only FreeType: no TTF parser or untrusted font bytes run in Ring 0.
"""
import argparse
import ctypes as C
import ctypes.util
import hashlib
from pathlib import Path

HASHES = {
    "Regular": "4637123e5ac90e21e9b16efc0ec8c6ee3a75d17727f33305137e3539b54eee59",
    "Bold": "f9a1855012a8e5c36d4522aa4e99ee1f8eb1ad565d35287e903d60f9a42a399c",
}
POINTS = list(range(32, 127)) + list(range(160, 256)) + [
    0x0152, 0x0153, 0x0160, 0x0161, 0x0178, 0x017d, 0x017e,
    0x0192, 0x02c6, 0x02dc, 0x2013, 0x2014, 0x2018, 0x2019,
    0x201a, 0x201c, 0x201d, 0x201e, 0x2020, 0x2021, 0x2022,
    0x2026, 0x2030, 0x2039, 0x203a, 0x20ac, 0x2122, 0x2190,
    0x2191, 0x2192, 0x2193, 0x2212, 0x2260, 0x2264, 0x2265,
]


class Generic(C.Structure):
    _fields_ = [("data", C.c_void_p), ("finalizer", C.c_void_p)]


class BBox(C.Structure):
    _fields_ = [(x, C.c_long) for x in ("xMin", "yMin", "xMax", "yMax")]


class Vector(C.Structure):
    _fields_ = [("x", C.c_long), ("y", C.c_long)]


class Bitmap(C.Structure):
    _fields_ = [
        ("rows", C.c_uint), ("width", C.c_uint), ("pitch", C.c_int),
        ("buffer", C.POINTER(C.c_ubyte)), ("num_grays", C.c_ushort),
        ("pixel_mode", C.c_ubyte), ("palette_mode", C.c_ubyte),
        ("palette", C.c_void_p),
    ]


class Metrics(C.Structure):
    _fields_ = [(x, C.c_long) for x in (
        "width", "height", "horiBearingX", "horiBearingY", "horiAdvance",
        "vertBearingX", "vertBearingY", "vertAdvance",
    )]


class Slot(C.Structure):
    _fields_ = [
        ("library", C.c_void_p), ("face", C.c_void_p), ("next", C.c_void_p),
        ("glyph_index", C.c_uint), ("generic", Generic), ("metrics", Metrics),
        ("linearHoriAdvance", C.c_long), ("linearVertAdvance", C.c_long),
        ("advance", Vector), ("format", C.c_uint), ("bitmap", Bitmap),
        ("bitmap_left", C.c_int), ("bitmap_top", C.c_int),
    ]


class Face(C.Structure):
    _fields_ = [(x, C.c_long) for x in (
        "num_faces", "face_index", "face_flags", "style_flags", "num_glyphs",
    )] + [
        ("family_name", C.c_char_p), ("style_name", C.c_char_p),
        ("num_fixed_sizes", C.c_int), ("available_sizes", C.c_void_p),
        ("num_charmaps", C.c_int), ("charmaps", C.c_void_p),
        ("generic", Generic), ("bbox", BBox), ("units_per_EM", C.c_ushort),
    ] + [(x, C.c_short) for x in (
        "ascender", "descender", "height", "max_advance_width",
        "max_advance_height", "underline_position", "underline_thickness",
    )] + [
        ("glyph", C.POINTER(Slot)), ("size", C.c_void_p),
        ("charmap", C.c_void_p),
    ]


def render(path, px):
    library_name = ctypes.util.find_library("freetype")
    if not library_name:
        raise RuntimeError("Host libfreetype6 is required to build the system font atlas")
    ft = C.CDLL(library_name)
    ft.FT_Init_FreeType.argtypes = [C.POINTER(C.c_void_p)]
    ft.FT_New_Face.argtypes = [C.c_void_p, C.c_char_p, C.c_long,
                              C.POINTER(C.c_void_p)]
    ft.FT_Set_Pixel_Sizes.argtypes = [C.c_void_p, C.c_uint, C.c_uint]
    ft.FT_Load_Char.argtypes = [C.c_void_p, C.c_ulong, C.c_int]
    ft.FT_Get_Char_Index.argtypes = [C.c_void_p, C.c_ulong]
    ft.FT_Get_Char_Index.restype = C.c_uint
    ft.FT_Done_Face.argtypes = [C.c_void_p]
    ft.FT_Done_FreeType.argtypes = [C.c_void_p]
    lib = C.c_void_p()
    face = C.c_void_p()
    if ft.FT_Init_FreeType(C.byref(lib)) != 0:
        raise RuntimeError("FT_Init_FreeType failed")
    try:
        if ft.FT_New_Face(lib, str(path).encode(), 0, C.byref(face)) != 0:
            raise RuntimeError("FT_New_Face failed")
        if ft.FT_Set_Pixel_Sizes(face, 0, px) != 0:
            raise RuntimeError("FT_Set_Pixel_Sizes failed")
        if C.cast(face, C.POINTER(Face)).contents.family_name != b"Aurora Celestia UI":
            raise RuntimeError("Typeface family mismatch")
        pixels = bytearray()
        glyphs = []
        for code in POINTS:
            if ft.FT_Get_Char_Index(face, code) == 0:
                continue
            if ft.FT_Load_Char(face, code, 4) != 0:
                raise RuntimeError(f"FT_Load_Char failed: U+{code:04X}")
            slot = C.cast(face, C.POINTER(Face)).contents.glyph.contents
            bmp = slot.bitmap
            if (bmp.pixel_mode != 2 or bmp.width > 48 or bmp.rows > 48
                    or bmp.pitch < 0 or bmp.pitch < bmp.width):
                raise RuntimeError(f"Invalid grayscale glyph: U+{code:04X}")
            offset = len(pixels)
            for row in range(bmp.rows):
                addr = C.cast(bmp.buffer, C.c_void_p).value
                pixels.extend(C.string_at(addr + row * bmp.pitch, bmp.width))
            if len(pixels) > 160000:
                raise RuntimeError("Font atlas exceeds fixed 160KiB budget")
            glyphs.append((code, offset, bmp.width, bmp.rows,
                           slot.bitmap_left, slot.bitmap_top,
                           slot.advance.x // 64))
        if len(glyphs) < 190 or 0x20AC not in {g[0] for g in glyphs}:
            raise RuntimeError("Expected Latin-1 and Euro glyphs missing")
        return glyphs, pixels
    finally:
        if face.value:
            ft.FT_Done_Face(face)
        ft.FT_Done_FreeType(lib)


def generate(fonts, output):
    lines = [
        "/* Autogenerated from verified Aurora Celestia UI v1.0 TTFs. */",
        "#ifndef AURORA_CELESTIA_ATLAS_GENERATED_H",
        "#define AURORA_CELESTIA_ATLAS_GENERATED_H",
        "#include <stdint.h>",
        "struct aurora_celestia_glyph { uint32_t codepoint; uint32_t offset;"
        " uint8_t width; uint8_t height; int8_t left; int8_t top;"
        " uint8_t advance; };",
    ]
    for style, px in (("regular", 18), ("bold", 30)):
        family = style.capitalize()
        font = fonts / f"AuroraCelestiaUI-{family}.ttf"
        if hashlib.sha256(font.read_bytes()).hexdigest() != HASHES[family]:
            raise RuntimeError(f"Unverified system font: {font}")
        glyphs, pixels = render(font, px)
        lines += [
            f"#define AURORA_CELESTIA_{style.upper()}_PX {px}u",
            f"#define AURORA_CELESTIA_{style.upper()}_COUNT {len(glyphs)}u",
            f"static const struct aurora_celestia_glyph aurora_celestia_{style}_glyphs[] = {{",
        ]
        lines += ["  {%du,%du,%du,%du,%d,%d,%du}," % g for g in glyphs]
        lines += [
            "};",
            f"static const uint8_t aurora_celestia_{style}_pixels[] = {{",
        ]
        for index in range(0, len(pixels), 24):
            lines.append("  " + ", ".join(str(x) for x in pixels[index:index + 24]) + ",")
        lines.append("};")
        print(f"{family}: {len(glyphs)} glyphs / {len(pixels)} grayscale bytes")
    lines.append("#endif")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--fonts", type=Path,
                        default=Path("assets/fonts/aurora-celestia/ui/v1.0"))
    parser.add_argument("--output", type=Path,
                        default=Path("kernel/include/aurora/celestia_atlas_generated.h"))
    args = parser.parse_args()
    generate(args.fonts, args.output)
