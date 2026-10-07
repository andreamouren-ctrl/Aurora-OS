#!/usr/bin/env python3
import argparse
import math

def st2084_eotf_nits(code: int) -> float:
    n = code / 65535.0
    m1 = 2610.0 / 16384.0
    m2 = 2523.0 / 32.0
    c1 = 3424.0 / 4096.0
    c2 = 2413.0 / 128.0
    c3 = 2392.0 / 128.0
    p = n ** (1.0 / m2)
    numerator = max(p - c1, 0.0)
    denominator = c2 - c3 * p
    if denominator <= 0.0:
        return 10000.0
    return 10000.0 * (numerator / denominator) ** (1.0 / m1)

def hlg_inverse_oetf(signal: float) -> float:
    a = 0.17883277
    b = 0.28466892
    c = 0.55991073
    if signal <= 0.5:
        return (signal * signal) / 3.0
    return (math.exp((signal - c) / a) + b) / 12.0

def q16_nits(value: float) -> int:
    return max(0, min(0xFFFFFFFF, int(round(value * 65536.0))))

def q16_unit(value: float) -> int:
    return max(0, min(65535, int(round(value * 65535.0))))

def emit(path: str) -> None:
    with open(path, "w", encoding="utf-8") as f:
        f.write("#ifndef AURORA_COLOR_LUTS_GENERATED_H\n")
        f.write("#define AURORA_COLOR_LUTS_GENERATED_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("#define AURORA_ST2084_Q16_ENTRIES 65536u\n\n")
        f.write("static const uint32_t aurora_st2084_nits_q16[AURORA_ST2084_Q16_ENTRIES] = {\n")
        row = []
        for code in range(65536):
            row.append(str(q16_nits(st2084_eotf_nits(code))))
            if len(row) == 8:
                f.write("    " + ", ".join(row) + ",\n")
                row = []
        if row:
            f.write("    " + ", ".join(row) + "\n")
        f.write("};\n\n")
        f.write("#define AURORA_HLG_Q16_ENTRIES 65536u\n\n")
        f.write("static const uint16_t aurora_hlg_scene_linear_q16[AURORA_HLG_Q16_ENTRIES] = {\n")
        row = []
        for code in range(65536):
            row.append(str(q16_unit(hlg_inverse_oetf(code / 65535.0))))
            if len(row) == 12:
                f.write("    " + ", ".join(row) + ",\n")
                row = []
        if row:
            f.write("    " + ", ".join(row) + "\n")
        f.write("};\n\n#endif\n")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    emit(args.output)

if __name__ == "__main__":
    main()
