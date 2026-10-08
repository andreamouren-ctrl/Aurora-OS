# Aurora OS G5 — WP-02 Integration Acceptance

Status: **BLOCKED — do not mark WP-02 closed without every runtime gate.**
Date: 2026-10-08. Ground truth is the referenced source and GitHub Actions logs.

## WP-02 four required acceptance gates

| Gate | Implemented evidence | Remaining requirement |
| --- | --- | --- |
| 1. Real IPC and authenticated sender | `g5_ipc_endpoint.c` consumes Aurora kernel `ipc_receive`; validates receiver-side IPC endpoint handle by `cap_lookup`, explicit authority, exclusive-capability provisioning and Ring 3 probe in `g5_ipc_ring3_probe.c`.  | Production session-scoped Shell and compositor services must provision the exclusive endpoint via Session Manager and run the real routing loops. No sender PID is carried by the existing IPC receive API; security derives from exclusive endpoint grants, not untrusted header fields. |
| 2. Concurrent requests and opcode rights | Atomic single-executor admission in `g5_ipc_dispatch.c`; thread-race test `ipc_concurrency_test.c`, strict opcode geometry/serial/generation semantics, differentiated READ / CONTROL / CONTROL+WRITE authorization, 16-entry bounded REQUEST/CANCEL queue. | Service owner and session-lifecycle review must confirm no untrusted second consumer, no handler side effect begins after session invalidation, no cross-principal endpoint delegation and correct response/backpressure semantics. |
| 3. Replay after crash and recovery | `g5_ipc_durable.c` persists a versioned, checksummed monotonic high-water mark via Aurora protected-state durable replacement **before** side effects; fault-injected host tests verify failed writes do not admit operations, corrupt records fail closed, restart replays are rejected. | QEMU cold reboot on the same AHCI disk must prove journal survival and recovery. This is at-most-once admission, NOT exactly-once completion. |
| 4. Functional Ring 3 / QEMU and CI | Ring 3 G5 framing roundtrip uses isolated existing Ring 3 user probe, IPC_WAIT, IPC_SEND/RECEIVE and kernel capability gate. `G5 IPC Wire Contract`, `Aurora OS Bootstrap Build`, `Aurora Identity Entropy Handoff`, and `G5 IPC QEMU Cold Boot Recovery` workflows. | Fresh **green** full-run evidence (host tests, x86_64 link, Ring 3 marker, consecutive QEMU boots with shared disk, no panic). Shell↔compositor and two-client UI semantics require WP-03 implementation. |

## Verification history

- PR #117: host IPC tests and x86_64/Identity workflow green; durable host injection and restart test.
- PR #118: Ring 3 probe added; Identity/QEMU smoke validates isolated Ring 3 roundtrip.
- PR #121: schema test fixture failed (stale upper bytes of 64-bit ACK generation); corrected in commit `e2155197`, rechecked by subsequent CI.
- PR #122: initial cold-boot QEMU failed due to durable replay probe; kernel must NOT suppress this failure. Stage diagnostics added later.
- PR #124: host G5 IPC suite green with differentiated operation rights.
- PR #125: outstanding failure-stage QEMU diagnostics; keep the release gate BLOCKED until a fresh passing execution is recorded.


- PR #127 (2026-10-08): cold-boot QEMU reproduced failure after successfully restoring the prior ledger (G5 probe stage **4**). `protected_state_replace_record_durable()` failed at stage **5**, specifically `vfs_rename(staging, target)` on AuroraFS v2. This is a **storage namespace replacement path** failure, not wire codec / first-boot persistence. Recovery test remains red. Investigate `aurora_fs_v2_rename_child_txn()` constraints (including source-last-directory-slot restriction and target inode preconditions) before any filesystem change; do not weaken atomicity.

## Security and scope notes

- Capability receiver table must be owned by the service; an integer in the IPC data payload never grants authority.
- A live sender's identity must be established during exclusive endpoint capability provisioning. The `provisioned_exclusively` boolean is **not independently attestation**; a trusted supervisor must own its lifecycle.
- A CANCEL is best effort and cannot roll back committed effects. Queue depth is bounded at 16. Never spin while holding compositor, capability or protected-state locks.
- Crash recovery cannot use stale user-session generations or previously consumed IDs. Durable reservation prevents duplicate execution but an interrupted transaction may remain unapplied.
- WP-02 cannot be declared complete merely because component host tests pass. G5-D32 full shared-memory/bulk path and a production multi-process Shell are explicitly gated on later implementation work.

## Final WP-02 release decision

No closure until the production service trust boundary and QEMU cold-reboot results are independently verified. If these gates fail, keep WP-02 BLOCKED and preserve the failing artifacts in GitHub Actions.
