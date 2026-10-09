#!/usr/bin/env python3
"""Rebuild the Aurora Identity Security Activity PNG bundle."""

import argparse
import base64
import hashlib
import io
import tarfile
from pathlib import Path

EXPECTED_SHA256 = "a28a2ff0008a5b4d3287356a3a1cc52b10f79e1da44040a8739649096bf26ed4"
EXPECTED_FILES = {
    "aurora_security_activity_panel.png",
    "aurora_security_activity_event_card.png",
    "aurora_security_activity_status_info.png",
    "aurora_security_activity_status_warning.png",
    "aurora_security_activity_status_critical.png",
    "aurora_security_activity_category_auth.png",
    "aurora_security_activity_category_session.png",
    "aurora_security_activity_category_credential.png",
    "aurora_security_activity_header.png",
    "aurora_security_activity_empty.png",
    "aurora_security_activity_error.png",
    "aurora_security_activity_loading.png",
    "aurora_security_activity_load_more.png",
    "aurora_security_activity_filter_button.png",
    "aurora_security_activity_filter_active.png",
    "aurora_security_activity_end.png",
}

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", nargs="+", required=True)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()

    encoded = "".join(Path(p).read_text(encoding="ascii").strip() for p in args.input)
    payload = base64.b64decode(encoded, validate=True)
    digest = hashlib.sha256(payload).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(f"security activity asset bundle SHA-256 mismatch: {digest}")

    destination = Path(args.output_dir)
    destination.mkdir(parents=True, exist_ok=True)
    seen = set()
    with tarfile.open(fileobj=io.BytesIO(payload), mode="r:gz") as archive:
        for member in archive.getmembers():
            name = Path(member.name).name
            if member.name != name or name not in EXPECTED_FILES or not member.isfile():
                raise SystemExit(f"unexpected security activity asset: {member.name}")
            stream = archive.extractfile(member)
            if stream is None:
                raise SystemExit(f"unable to read security activity asset: {name}")
            data = stream.read()
            if not data.startswith(b"\x89PNG\r\n\x1a\n"):
                raise SystemExit(f"security activity asset is not PNG: {name}")
            (destination / name).write_bytes(data)
            seen.add(name)

    if seen != EXPECTED_FILES:
        missing = sorted(EXPECTED_FILES - seen)
        raise SystemExit(f"security activity asset bundle incomplete: {missing}")

    print(f"Prepared {len(seen)} Security Activity assets, sha256={digest}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
