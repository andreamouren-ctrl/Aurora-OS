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


- PR #131 (2026-10-08, commit `1ba23ea4`): **all three workflows green** — G5 IPC QEMU Cold Boot Recovery `37781500265`, Identity Entropy Handoff `37781500330`, and Aurora OS Bootstrap Build `37781500474`. The QEMU two-boot ledger recovery passes on the same disk after correcting `v2d_rename()` to permit journaled regular-file replacement. This closes the **cold-boot replay verification blocker**, not the outstanding production Shell principal-provisioning/revocation gate.


- Session-host production boundary increment (2026-10-08): `user_session_host_abi.h` v2 exposes optional `g5_endpoint` without growing the 64-byte startup block. `user_session_host.c` provisions the only sender capability directly to the authenticated Ring 3 User Session Host process with WRITE-only (no TRANSFER), retains READ-only receiver authority in kernel, binds dispatcher to trusted Session Manager generation, and consumes one framed `SHELL_READY` before declaring host active. Ring 3 runtime sends READY over the dedicated endpoint. Sender grant is revoked at logout and teardown. Boot self-test explicitly registers an authorized test dispatcher and verifies one accepted READY. PR #134 is the fresh CI acceptance gate; until all checks pass this is **implemented but not accepted**. This is a session-host bootstrap control-plane, **not yet a full graphical Shell/compositor service**.


- Entropy regression check (2026-10-08): PR #134 Identity/Entropy job `37782570946` **failed** after early RDSEED readiness, later `[ring3-entropy] trusted seed unavailable; capability probe skipped`. PR #135 (diagnostic commit `9e33765d`) introduces read-only status telemetry (`health_failed`, source failures, health failures, samples and output words). The first subsequent Identity/Entropy run `37783441649` completed **successfully**. Because the previously observed drop was intermittent, this is **not proof that the underlying problem was fixed**; keep the prior failure recorded and preserve fail-closed behavior. A future failing run with telemetry is needed to distinguish source-health failure from other state changes.


- CI configuration diagnosis (2026-10-08): PR #136 `Aurora OS Bootstrap Build` failed when QEMU reported **RDSEED unavailable, RDRAND unavailable**, with zero startup samples/failures. Unlike the Identity smoke, `.github/workflows/build.yml` invoked its three QEMU boots without `-cpu max`. Commit `ec25cad2` sets `-cpu max` for all three, preserving mandatory Ring 3 entropy validation and hardware fail-closed semantics. PR #137 executes verification on this revised CPU profile. **Do not interpret a green CPU-configured QEMU run as proof of deterministic RDSEED availability on all physical machines.**

## Security and scope notes

- Capability receiver table must be owned by the service; an integer in the IPC data payload never grants authority.
- A live sender's identity must be established during exclusive endpoint capability provisioning. The `provisioned_exclusively` boolean is **not independently attestation**; a trusted supervisor must own its lifecycle.
- A CANCEL is best effort and cannot roll back committed effects. Queue depth is bounded at 16. Never spin while holding compositor, capability or protected-state locks.
- Crash recovery cannot use stale user-session generations or previously consumed IDs. Durable reservation prevents duplicate execution but an interrupted transaction may remain unapplied.
- WP-02 cannot be declared complete merely because component host tests pass. G5-D32 full shared-memory/bulk path and a production multi-process Shell are explicitly gated on later implementation work.

## Final WP-02 release decision

No closure until the production service trust boundary and QEMU cold-reboot results are independently verified. If these gates fail, keep WP-02 BLOCKED and preserve the failing artifacts in GitHub Actions.
