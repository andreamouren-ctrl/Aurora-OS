#!/usr/bin/env python3
"""Rebuild the early-boot Aurora artwork from its repository text asset."""

import argparse
import base64
import hashlib
from pathlib import Path
import zlib

EXPECTED_SIZE = 28416
EXPECTED_SHA256 = "1863a27d7e6e3868dab257e1b14cd1db9384333857621048e630a2edb4ee8d95"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", nargs="+", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    encoded = "".join(
        Path(path).read_text(encoding="ascii").strip()
        for path in args.input
    )

    compressed = base64.b64decode(encoded, validate=True)
    payload = zlib.decompress(compressed)

    if len(payload) != EXPECTED_SIZE:
        raise SystemExit(
            f"boot asset size mismatch: {len(payload)} != {EXPECTED_SIZE}"
        )

    digest = hashlib.sha256(payload).hexdigest()

    if digest != EXPECTED_SHA256:
        raise SystemExit(
            f"boot asset SHA-256 mismatch: {digest}"
        )

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload)

    print(
        f"Prepared Aurora boot artwork: {len(payload)} bytes, sha256={digest}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
