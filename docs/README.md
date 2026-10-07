# Aurora OS Documentation Index

This directory contains the canonical project documentation. Documents are grouped by purpose so implementation status is not confused with future design goals.

## Current system status

- [`SYSTEM_AUDIT_2026-10-07.md`](SYSTEM_AUDIT_2026-10-07.md) — cross-subsystem implementation audit and Aurora vs Ubuntu/Linux vs Windows comparison.
- [`ROADMAP.md`](ROADMAP.md) — dependency-ordered implementation roadmap.
- [`KERNEL_MODEL.md`](KERNEL_MODEL.md) — current kernel/service responsibility split.

The audit document is the preferred high-level status snapshot. Subsystem documents remain authoritative for their own contracts and implementation detail.

## Project direction

- [`VISION.md`](VISION.md) — product and operating-system vision.
- [`ARCHITECTURE_PRINCIPLES.md`](ARCHITECTURE_PRINCIPLES.md) — architectural rules and boundaries.
- [`SYSCALL_ABI.md`](SYSCALL_ABI.md) — syscall ABI direction.
- [`RING3_IPC_SYSCALLS.md`](RING3_IPC_SYSCALLS.md) — capability-checked Ring 3 IPC send/receive/wait ABI and runtime probes.
- [`RING3_USER_MEMORY.md`](RING3_USER_MEMORY.md) — anonymous/shared Ring 3 memory foundations.
- [`TRUSTED_SERVICE_BOOTSTRAP.md`](TRUSTED_SERVICE_BOOTSTRAP.md) — least-authority trusted Ring 3 service bootstrap.
- [`PROCESS_LIFECYCLE.md`](PROCESS_LIFECYCLE.md) — Ring 3 process/thread resource ownership, reap ordering and lifecycle-reuse verification.
- [`SERVICE_SUPERVISION.md`](SERVICE_SUPERVISION.md) — trusted-service restart/reconstruction model.

## Security and permissions

- [`APPLICATION_PERMISSIONS.md`](APPLICATION_PERMISSIONS.md) — application permission model.
- [`PROTECTED_SYSTEM_STATE.md`](PROTECTED_SYSTEM_STATE.md) — capability-gated durable state for trusted system services.
- [`adr/`](adr/) — architecture decision records. ADRs are historical records and are superseded by later ADRs rather than silently rewritten.

## Aurora Identity and sessions

- [`AURORA_IDENTITY.md`](AURORA_IDENTITY.md) — master Aurora Identity product/security specification.
- [`identity/README.md`](identity/README.md) — detailed Identity documentation index.
- [`identity/IMPLEMENTATION_ROADMAP.md`](identity/IMPLEMENTATION_ROADMAP.md) — live Identity/Session implementation sequencing.

The live repository now includes a Ring 3 Identity Service, one-time session grants, a Ring 3 Session Manager, persistent profile capabilities, logout/lock/unlock/terminal lifecycle handling and an ordinary Ring 3 User Session Host. Documents that describe Identity as an isolated host-only core are obsolete and should be corrected when found.

## Graphics and desktop

- [`graphics/README.md`](graphics/README.md) — graphics/display/compositor/windowing contract set.
- [`graphics/IMPLEMENTATION_ROADMAP.md`](graphics/IMPLEMENTATION_ROADMAP.md) — live M4 implementation status.

Current M4 status at the 2026-10-07 audit baseline:

- G0 contracts: complete;
- G1 display foundation: complete/runtime verified;
- G2 surface/buffer core: complete/runtime verified;
- G3 software compositor + professional color pipeline: complete/runtime verified;
- G4 pointer/modern input: active implementation.

## Boot and UI bootstrap

- [`BOOT_UI.md`](BOOT_UI.md) — framebuffer boot/recovery UI and boot/login bootstrap behavior.

The framebuffer renderer is retained as an independent recovery/fallback path even as normal graphics moves into the M4 compositor architecture.

## Storage

- [`AURORAFS.md`](AURORAFS.md) — native AuroraFS design direction.
- [`AURORAFS_V2_LEVEL3_COMPLETION.md`](AURORAFS_V2_LEVEL3_COMPLETION.md) — closed bounded Level-3 hierarchy milestone.
- [`FILESYSTEM_COMPATIBILITY.md`](FILESYSTEM_COMPATIBILITY.md) — current foreign-filesystem and transport compatibility matrix.
- [`AHCI.md`](AHCI.md) — SATA/AHCI baseline and remaining advanced work.
- [`NVME.md`](NVME.md) — NVMe baseline and remaining advanced work.

The modern AHCI and NVMe paths are runtime verified through the common block/partition/filesystem/VFS architecture in QEMU. USB/xHCI mass storage remains future work.

## Documentation rules

1. **Designed** is not the same as **implemented**.
2. **Implemented** is not the same as **runtime verified**.
3. **Runtime verified in QEMU** is not the same as **real-hardware certified**.
4. Runtime claims should identify the tested path or acceptance gate where practical.
5. Filesystem signature recognition must not be described as full filesystem support.
6. Bootstrap limits must not be presented as production architecture limits.
7. Historical ADRs are retained; stale ordinary status documents must be updated rather than preserved as if current.
8. Root README and canonical roadmaps must reflect the current repository, not an earlier project phase.
9. When code closes a roadmap gate, the related roadmap/status document must be updated in the same development block whenever practical.
10. Competitive comparisons must distinguish architecture quality from production maturity and ecosystem breadth.

## Verification policy

Aurora uses several independent CI/runtime families, including:

- generic bootstrap/SMP/Ring 3 validation;
- AHCI filesystem end-to-end;
- NVMe probe;
- NVMe filesystem end-to-end;
- Identity core/runtime tests;
- entropy/Identity handoff tests.

A single historical workflow number is intentionally not treated as the permanent global status reference because the implementation evolves rapidly. The current branch/head and its required workflow gates are the authoritative verification source.

