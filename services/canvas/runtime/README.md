# Canvas Engine Ring3 runtime

This is an isolated, freestanding x86-64 executable built by
`make -C services/canvas/runtime`. The link must contain no undefined
symbols. It is packaged as a distinct image in the kernel build. The Session Manager
launches it as a short-lived Ring3 smoke probe and requires a clean exit
before marking the G5 session ready. It is **not** a persistent service.

## Current bootstrap boundary

The entry trampoline invokes `g5_canvas_runtime_main`, which revokes its
empty camera state and exits. This is intentionally fail-closed: there is no
session generation, delegated IPC endpoint, surface authority, or committed
scene to operate on.

## Required activation gates

1. [Implemented, pending CI/QEMU validation] Package `canvas-engine.bin`
   separately from `user-session-host.bin` and smoke-launch its process.
2. [Partially implemented] Launch in a separate process/address space.
   A unique control IPC endpoint and validated Ring3 startup generation
   are still required for the persistent service.
3. Prove READY/HEALTH, shutdown and stale-generation rejection.
4. Route camera transactions from Shell to Canvas, then project bounded
   positions and publish them through the trusted compositor coordinator.
5. Verify QEMU end-to-end pan/zoom, surface revocation and restart.

Never treat successful host camera tests or a successful binary build as proof
that any of these runtime gates has passed.
