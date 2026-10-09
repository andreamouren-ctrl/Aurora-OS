#!/usr/bin/env python3
"""Fail the build if Ring3 entry assembly truncates the User Session Host ABI."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
header = (root / "kernel/include/aurora/user_session_host_abi.h").read_text()
entry = (root / "services/user_session/runtime/entry.S").read_text()
match = re.search(
    r"^#define\s+AURORA_USER_SESSION_HOST_STARTUP_STACK_OFFSET\s+(\d+)u\s*$",
    header, re.M,
)
if not match:
    sys.exit("missing User Session Host startup offset")
size = int(match.group(1))
if size == 0 or size % 8:
    sys.exit(f"invalid User Session Host startup size: {size}")
reads = [int(v) for v in re.findall(r"^\s*movq\s+(-?\d+)\(%r11\),\s*%rax\s*$", entry, re.M)]
writes = [int(v) for v in re.findall(r"^\s*movq\s+%rax,\s*(\d+)\(%rsp\)\s*$", entry, re.M)]
expected_reads = list(range(-size, 0, 8))
expected_writes = list(range(32, 32 + size, 8))
if reads != expected_reads or writes != expected_writes:
    sys.exit(
        f"entry.S startup copy is inconsistent with ABI ({size} bytes): "
        f"reads={reads}, writes={writes}"
    )
rdi = re.search(r"^\s*leaq\s+(\d+)\(%rsp\),\s*%rdi\s*$", entry, re.M)
if not rdi or int(rdi.group(1)) != 32 + size:
    sys.exit("entry.S initial_rsp does not point beyond the startup copy")
frames = re.findall(r"^\s*subq\s+\$(\d+),\s*%rsp\s*$", entry, re.M)
restores = re.findall(r"^\s*addq\s+\$(\d+),\s*%rsp\s*$", entry, re.M)
if len(frames) != 1 or len(restores) != 1 or frames[0] != restores[0]:
    sys.exit("unbalanced User Session Host entry stack frame")
if int(frames[0]) < 32 + size or int(frames[0]) % 16:
    sys.exit("entry.S frame too small or misaligned for startup")
print(f"Ring3 User Session Host ABI/entry copy verified ({size} bytes)")
