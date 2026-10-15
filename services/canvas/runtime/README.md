# Canvas Engine Ring3 runtime

This is an isolated, freestanding x86-64 executable built by
`make -C services/canvas/runtime`. The link must contain no undefined
symbols. It is **not** currently packaged into the kernel image or launched
by the Session Manager.

## Current bootstrap boundary

The entry trampoline invokes `g5_canvas_runtime_main`, which revokes its
empty camera state and exits. This is intentionally fail-closed: there is no
session generation, delegated IPC endpoint, surface authority, or committed
scene to operate on.

## Required activation gates

1. Package `canvas-engine.bin` separately from `user-session-host.bin`.
2. Launch it in its own process/address space, with a unique control IPC
   endpoint and validated startup generation.
3. Prove READY/HEALTH, shutdown and stale-generation rejection.
4. Route camera transactions from Shell to Canvas, then project bounded
   positions and publish them through the trusted compositor coordinator.
5. Verify QEMU end-to-end pan/zoom, surface revocation and restart.

Never treat successful host camera tests or a successful binary build as proof
that any of these runtime gates has passed.
