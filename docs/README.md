# Aurora OS Documentation Index

This directory contains the canonical project documentation. Documents are grouped by purpose so implementation status is not confused with future design goals.

## Project direction

- [`VISION.md`](VISION.md) — product and operating-system vision.
- [`ARCHITECTURE_PRINCIPLES.md`](ARCHITECTURE_PRINCIPLES.md) — architectural rules and boundaries.
- [`ROADMAP.md`](ROADMAP.md) — implementation and verification roadmap.
- [`KERNEL_MODEL.md`](KERNEL_MODEL.md) — current kernel model and responsibility split.
- [`SYSCALL_ABI.md`](SYSCALL_ABI.md) — syscall ABI direction.

## Security and permissions

- [`APPLICATION_PERMISSIONS.md`](APPLICATION_PERMISSIONS.md) — application permission model.
- [`adr/`](adr/) — architecture decision records that must remain stable historical records unless explicitly superseded by a later ADR.

## Aurora Identity

- [`AURORA_IDENTITY.md`](AURORA_IDENTITY.md) — master Aurora Identity product specification.
- [`identity/README.md`](identity/README.md) — index for the detailed Identity specification set.

The files under `identity/` are not duplicates of the master document: they split the architecture, service contract, Aurora Key, storage model, security model, IPC, UX, recovery, test plan, removable authenticator design, and implementation roadmap into focused specifications.

## Boot and UI bootstrap

- [`BOOT_UI.md`](BOOT_UI.md) — framebuffer boot UI and boot/login bootstrap behavior.

## Storage

- [`AURORAFS.md`](AURORAFS.md) — native AuroraFS direction and bootstrap v1 persistence format.
- [`FILESYSTEM_COMPATIBILITY.md`](FILESYSTEM_COMPATIBILITY.md) — current foreign-filesystem compatibility and runtime verification matrix.

## Documentation rules

1. **Designed** is not the same as **implemented**.
2. **Implemented** is not the same as **runtime verified**.
3. Runtime claims must identify the tested path where practical.
4. Filesystem signature recognition must not be described as full filesystem support.
5. Bootstrap limits must not be presented as production architecture limits.
6. Historical ADRs are retained even when a later ADR supersedes them; ordinary obsolete drafts and unused generated assets should be removed.
7. README status summaries should reflect the current repository and CI, not planned work.

## Current verified storage reference

The combined ATA PIO + MBR + AuroraFS + FAT32/VFAT + exFAT runtime test is green in GitHub Actions workflow **#271** (`36888671373`) at commit `483ab55e803b7d22c061d938ffdd6b23e5e86bb7`.

This verifies the current software path in QEMU. It does not constitute real-hardware certification.