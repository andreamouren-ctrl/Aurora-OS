#!/usr/bin/env python3
"""Source-level regression guard for kernel IPC syscall cleanup.

This checks structural invariants; it is not a runtime memory-erasure test.
"""
from pathlib import Path
import re

SOURCE = Path("kernel/src/arch/x86_64/syscall.c").read_text()


def function_region(source: str, start: str, end: str) -> str:
    first = source.find(start)
    last = source.find(end, first + len(start))
    if first < 0 or last <= first:
        raise AssertionError(f"Missing function boundary: {start}")
    return source[first:last]


def check(source: str) -> None:
    if "static void secure_zero_bytes(" not in source:
        raise AssertionError("secure zero helper missing")

    send = function_region(
        source, "static uint64_t dispatch_ipc_send(",
        "static uint64_t dispatch_ipc_receive("
    )
    receive = function_region(
        source, "static uint64_t dispatch_ipc_receive(",
        "static uint64_t dispatch_entropy_seed("
    )

    for label, body, buffers in (
        ("send", send, ("payload", "user_transfer_buffer", "transfers")),
        ("receive", receive, ("&received", "&output")),
    ):
        if "cleanup:" not in body or body.count("return ") != 1:
            raise AssertionError(f"{label}: cleanup may be bypassed")
        if not re.search(
            r"cleanup:\s*(?:secure_zero_bytes\([^;]+;\s*)+return ", body
        ):
            raise AssertionError(f"{label}: cleanup does not precede return")
        for buffer in buffers:
            pattern = rf"secure_zero_bytes\(\s*{re.escape(buffer)}\s*,\s*sizeof\("
            if not re.search(pattern, body):
                raise AssertionError(f"{label}: missing wipe of {buffer}")

    if "ipc_received_revoke_capabilities(&process->capabilities, &received)" not in receive:
        raise AssertionError("receive: missing capability rollback")


def expect_rejection(source: str, description: str) -> None:
    try:
        check(source)
    except AssertionError:
        return
    raise AssertionError(f"Regression guard missed mutation: {description}")


check(SOURCE)

# Self-test the guard against representative security regressions.
def mutated(original: str, replacement: str) -> str:
    if SOURCE.count(original) != 1:
        raise AssertionError(f"Mutation anchor missing or ambiguous: {original}")
    return SOURCE.replace(original, replacement, 1)


for original, description in (
    ("secure_zero_bytes(payload, sizeof(payload));", "removed payload wipe"),
    ("secure_zero_bytes(user_transfer_buffer, sizeof(user_transfer_buffer));",
     "removed user capability staging wipe"),
    ("secure_zero_bytes(transfers, sizeof(transfers));", "removed transfer wipe"),
    ("secure_zero_bytes(&received, sizeof(received));", "removed received wipe"),
    ("secure_zero_bytes(&output, sizeof(output));", "removed output wipe"),
    ("ipc_received_revoke_capabilities(&process->capabilities, &received);",
     "removed capability rollback"),
):
    expect_rejection(mutated(original, "/* intentionally removed */"), description)

# Both send and receive contain this guard: mutate only the send region.
send_start = SOURCE.index("static uint64_t dispatch_ipc_send(")
receive_start = SOURCE.index("static uint64_t dispatch_ipc_receive(")
send_region = SOURCE[send_start:receive_start]
guard = "if (endpoint == NULL) goto cleanup;"
if send_region.count(guard) != 1:
    raise AssertionError("Send endpoint guard missing or ambiguous")
expect_rejection(
    SOURCE[:send_start]
    + send_region.replace(
        guard, "if (endpoint == NULL) return AURORA_SYS_RESULT_ERROR;", 1
    )
    + SOURCE[receive_start:],
    "early return bypass",
)
# Exercise the receive-side early-return guard independently.
receive_end = SOURCE.index("static uint64_t dispatch_entropy_seed(")
receive_region = SOURCE[receive_start:receive_end]
if receive_region.count(guard) != 1:
    raise AssertionError("Receive endpoint guard missing or ambiguous")
expect_rejection(
    SOURCE[:receive_start]
    + receive_region.replace(
        guard, "if (endpoint == NULL) return AURORA_SYS_RESULT_ERROR;", 1
    )
    + SOURCE[receive_end:],
    "receive early return bypass",
)
print("IPC syscall cleanup invariants and mutation checks: PASS")
