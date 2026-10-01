#!/usr/bin/env python3
"""Rebuild the early-boot Aurora artwork from repository text chunks."""

import argparse
import base64
import hashlib
import lzma
from pathlib import Path

EXPECTED_SIZE = 921792
EXPECTED_SHA256 = "bbc97c0b9bffcc0736d92bad2aa14a39b1d2149cfc7db7dc9a57a6a490ff0eb9"


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
    payload = lzma.decompress(compressed)

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
        f"Prepared Aurora HD boot artwork: {len(payload)} bytes, sha256={digest}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
