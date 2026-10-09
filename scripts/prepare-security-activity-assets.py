#!/usr/bin/env python3
"""Rebuild the deterministic Aurora Identity Security Activity PNG catalog."""

import argparse
import base64
import hashlib
import io
import struct
import tarfile
from pathlib import Path

EXPECTED_SHA256 = "258828456431ac46ac5494539c7d77fb48d1a467757bee5e844a3640de390253"
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
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def validate_png(name: str, data: bytes) -> None:
    if len(data) < 33 or not data.startswith(PNG_SIGNATURE):
        raise SystemExit(f"security activity asset is not a valid PNG: {name}")
    if data[12:16] != b"IHDR":
        raise SystemExit(f"security activity asset lacks leading IHDR: {name}")

    width, height = struct.unpack(">II", data[16:24])
    bit_depth = data[24]
    color_type = data[25]
    compression = data[26]
    filtering = data[27]
    interlace = data[28]
    if width == 0 or height == 0:
        raise SystemExit(f"security activity asset has zero dimensions: {name}")
    if bit_depth != 8 or color_type not in (2, 6):
        raise SystemExit(
            f"security activity asset format unsupported by Aurora PNG decoder: {name}"
        )
    if compression != 0 or filtering != 0 or interlace != 0:
        raise SystemExit(
            f"security activity asset uses unsupported PNG encoding: {name}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", nargs="+", required=True)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()

    encoded = "".join(
        Path(path).read_text(encoding="ascii").strip()
        for path in args.input
    )
    try:
        payload = base64.b64decode(encoded, validate=True)
    except Exception as exc:
        raise SystemExit(
            f"security activity asset bundle strict Base64 decode failed: {exc}"
        ) from exc

    digest = hashlib.sha256(payload).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(
            f"security activity asset bundle SHA-256 mismatch: {digest}"
        )

    destination = Path(args.output_dir)
    destination.mkdir(parents=True, exist_ok=True)

    extracted: dict[str, bytes] = {}
    with tarfile.open(fileobj=io.BytesIO(payload), mode="r:gz") as archive:
        for member in archive.getmembers():
            name = Path(member.name).name
            if (
                member.name != name
                or name not in EXPECTED_FILES
                or not member.isfile()
                or name in extracted
            ):
                raise SystemExit(
                    f"unexpected Security Activity asset member: {member.name}"
                )
            stream = archive.extractfile(member)
            if stream is None:
                raise SystemExit(
                    f"unable to read Security Activity asset: {name}"
                )
            data = stream.read()
            validate_png(name, data)
            extracted[name] = data

    if set(extracted) != EXPECTED_FILES:
        missing = sorted(EXPECTED_FILES - set(extracted))
        raise SystemExit(
            f"security activity asset bundle incomplete: {missing}"
        )

    # Publish only after the complete archive has passed every validation.
    for name in sorted(extracted):
        target = destination / name
        temporary = destination / f".{name}.tmp"
        temporary.write_bytes(extracted[name])
        temporary.replace(target)

    print(
        f"Prepared {len(extracted)} Security Activity assets, "
        f"sha256={digest}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
