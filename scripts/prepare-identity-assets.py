#!/usr/bin/env python3
"""Rebuild full-quality Aurora Identity PNG assets from repository text chunks."""

import argparse
import base64
import hashlib
from pathlib import Path


def rebuild(inputs: list[str], output: str, expected_sha256: str) -> None:
    encoded = "".join(Path(path).read_text(encoding="ascii").strip() for path in inputs)
    payload = base64.b64decode(encoded, validate=True)
    digest = hashlib.sha256(payload).hexdigest()
    if digest != expected_sha256:
        raise SystemExit(
            f"identity asset SHA-256 mismatch: {digest} != {expected_sha256}"
        )
    if not payload.startswith(b"\x89PNG\r\n\x1a\n"):
        raise SystemExit("identity asset is not a PNG stream")

    path = Path(output)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    print(f"Prepared {path}: {len(payload)} bytes, sha256={digest}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--background-input", nargs="+", required=True)
    parser.add_argument("--mark-input", nargs="+", required=True)
    parser.add_argument("--background-output", required=True)
    parser.add_argument("--mark-output", required=True)
    args = parser.parse_args()

    rebuild(
        args.background_input,
        args.background_output,
        "7e887f0191706e2110217261bb2d3b28c5527a0a70ef13c6954a9fbcb94ba677",
    )
    rebuild(
        args.mark_input,
        args.mark_output,
        "a13b28fbbb150b9aa383f17f817b5a8782d4ce15b7c2a7f229320a21431cf2d2",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
