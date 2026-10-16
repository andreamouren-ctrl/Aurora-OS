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
expect_rejection(
    SOURCE.replace(
        "secure_zero_bytes(transfers, sizeof(transfers));",
        "/* transfer wipe removed */",
        1,
    ),
    "removed transfer wipe",
)
expect_rejection(
    SOURCE.replace(
        "secure_zero_bytes(&received, sizeof(received));",
        "/* received wipe removed */",
        1,
    ),
    "removed received wipe",
)
expect_rejection(
    SOURCE.replace(
        "ipc_received_revoke_capabilities(&process->capabilities, &received);",
        "/* rollback removed */",
        1,
    ),
    "removed capability rollback",
)
expect_rejection(
    SOURCE.replace(
        "if (endpoint == NULL) goto cleanup;",
        "if (endpoint == NULL) return AURORA_SYS_RESULT_ERROR;",
        1,
    ),
    "early return bypass",
)
print("IPC syscall cleanup invariants and mutation checks: PASS")
