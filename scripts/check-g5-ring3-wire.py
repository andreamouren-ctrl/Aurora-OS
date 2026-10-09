#!/usr/bin/env python3
"""Guard the Ring3 Shell G5 wire header against flags/payload offset confusion."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
runtime = (root / "services/user_session/runtime/main.c").read_text()
codec = (root / "kernel/src/ipc/g5_ipc_codec.c").read_text()
# The authoritative kernel encoder defines the v1 fixed header positions.
for field, offset in (("flags",16),("payload_bytes",20)):
    pattern = rf"put32\(dst\+{offset},h->{field}\)"
    if not re.search(pattern, codec):
        sys.exit(f"kernel wire encoder mismatch for {field}")
for func, payload_bytes in (
    ("send_g5_scene_publish",16),
    ("send_g5_window_place",24),
):
    begin=runtime.find("static bool "+func+"(")
    if begin<0: sys.exit(f"missing Ring3 encoder {func}")
    end=runtime.find("\nstatic ",begin+12)
    body=runtime[begin:end if end>=0 else None]
    if not re.search(rf"wire\[20\]\s*=\s*{payload_bytes}u",body):
        sys.exit(f"{func}: payload_bytes not encoded at byte 20")
    if re.search(r"wire\[16\]\s*=",body):
        sys.exit(f"{func}: nonzero flags at byte 16")
print("Ring3 Shell G5 wire header flags/payload offsets verified")
