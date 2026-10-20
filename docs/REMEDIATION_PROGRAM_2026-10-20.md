# Aurora OS — Remediation Program (2026-10-20)

Status: **open / not a release certification**. Baseline: current `main` at branch creation. This document tracks verified engineering closure, not planned features as if complete.

## Rules
- Each fix uses a focused branch/PR; do not merge failing or unreviewed changes.
- Evidence required: exact commit SHA, commands, host tests, QEMU first-boot and cold-reboot logs where applicable, negative/fault tests, CI run URLs, and explicit residual risks.
- Never run destructive filesystem probes on user disks. Use disposable images and backed-up test hardware.
- A green compilation alone never closes a security, persistence, or runtime gate.
- Keep README, ROADMAP and subsystem documentation synchronized in the same PR.

## P0: Trust and data integrity
1. **Kernel attack surface:** review syscall validation, usercopy, Ring 3 returns, IPC message length/overflow, capability transfer/revocation, races, process reap and service restart. Add fuzz/property and adversarial negative tests; no unresolved critical/high findings before release.
2. **Identity:** verify Argon2id parameters and normalization, secret lifecycle/scrubbing, entropy failures, throttling across reboot, one-time grant replay, session generation fencing, lock/logout and fail-closed crash recovery. Document threat model and evidence.
3. **AuroraFS v2:** create crash/power-loss matrix around create/write/truncate/rename/remove/fsync and COW publication; test corruption, ENOSPC, partial writes, interrupted flush and recovery on disposable images. Require reopen checks and invariants.
4. **Supply chain/CI:** pin external dependencies and verify hashes, least-privilege workflow permissions, build reproducibility, static analysis, sanitizers for host-testable modules, SBOM and secret scanning. Verify every required CI job on release SHA.

## P1: Functional release blockers
5. **Desktop G5:** close #213 only after authorized Ring 3 camera IPC, actual dual-client pan/zoom, committed-frame hit testing, lock/crash/cold-reboot gates and input-to-present latency measurements. Do not put unsafe FP state in Ring 3.
6. **Networking:** staged NIC discovery/driver, bounded packet buffers, Ethernet/ARP/IPv4/IPv6, UDP/TCP, DNS, TLS trust store, socket permissions and hostile-input testing. Treat production networking as blocked until independently verified.
7. **Audio:** controller and codec discovery, Ring 3 service isolation, PCM streams, mixer, device recovery, underrun and latency tests.
8. **Memory:** implement telemetry and pressure accounting before reclaim, freezing, compression or paging; verify memory exhaustion and fairness.
9. **Devices/GPU/power:** qualify xHCI mass storage, input and hotplug; GPU isolation and presentation; ACPI suspend/resume, thermal/battery safety, and real-hardware matrix.

## P2: Distribution readiness
10. **Packages and updates:** signed manifests, explicit permission broker, immutable/transactional system updates, rollback and recovery media.
11. **Stable developer surface:** versioned ABI/SDK, sample applications, conformance tests, accessibility, diagnostics and installer.
12. **Release gate:** performance baselines, soak testing, real hardware matrix, incident response, license/dependency review and clear supported/unsupported device list.

## Execution order
- Wave A: P0 code/security/storage/CI evidence and deterministic regression tests.
- Wave B: G5 completion and core networking/audio/memory vertical slices.
- Wave C: hardware breadth, distribution and release qualification.

## Tracking
- Existing umbrella blocker: #192.
- G5 WP-05 runtime acceptance: #213.
- Each workstream needs its own owner, scoped PR, executable acceptance criteria, and link to passing CI. No issue is closed based on this plan alone.
