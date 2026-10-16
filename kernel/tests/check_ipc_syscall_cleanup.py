#!/usr/bin/env python3
"""Static regression guard for syscall IPC staging cleanup.

This checks source structure, not runtime erasure or capability lifetime safety.
"""
from pathlib import Path
import re

source = Path("kernel/src/arch/x86_64/syscall.c").read_text()


def body_between(start: str, end: str) -> str:
    first = source.find(start)
    last = source.find(end, first + len(start))
    assert first >= 0 and last > first, f"Missing function boundary: {start}"
    return source[first:last]


send = body_between("static uint64_t dispatch_ipc_send(", "static uint64_t dispatch_ipc_receive(")
receive = body_between("static uint64_t dispatch_ipc_receive(", "static uint64_t dispatch_entropy_seed(")

assert "static void secure_zero_bytes(" in source
for label, body, buffers in (
    ("send", send, ("payload", "user_transfer_buffer", "transfers")),
    ("receive", receive, ("&received", "&output")),
):
    assert "cleanup:" in body, f"{label}: missing cleanup label"
    assert body.count("return ") == 1, f"{label}: bypassable cleanup return"
    assert re.search(r"cleanup:\s*(?:secure_zero_bytes\([^;]+;\s*)+return ", body), (
        f"{label}: cleanup must precede the sole return"
    )
    for buffer in buffers:
        pattern = rf"secure_zero_bytes\(\s*{re.escape(buffer)}\s*,\s*sizeof\("
        assert re.search(pattern, body), f"{label}: missing wipe of {buffer}"

assert "ipc_received_revoke_capabilities(&process->capabilities, &received)" in receive
print("IPC syscall cleanup source invariants: PASS")
