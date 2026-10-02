# Aurora OS AHCI Transport

Status: **runtime-verified baseline; advanced features pending**

This document defines the current AHCI storage transport state and the implementation contract for modern SATA data I/O.

## Architecture role

AHCI is a hardware transport beneath the generic Aurora block-device layer:

`PCI -> AHCI controller -> SATA port -> AHCI block device -> partition manager -> filesystem -> VFS`

Filesystem support remains transport-independent. FAT32, exFAT and AuroraFS contain no AHCI-specific behavior.

## Current implementation state

### Implemented

- PCI class/subclass/programming-interface discovery for AHCI controllers;
- BAR5 / ABAR discovery;
- PCI configuration-space write support;
- enabling PCI Memory Space and Bus Master command bits;
- non-cacheable ABAR MMIO mapping into kernel virtual memory;
- enabling AHCI mode through `GHC.AE`;
- reading controller CAP, VS and PI registers;
- enumerating implemented ports;
- reading port SSTS and SIG registers;
- identifying active SATA ATA-device ports;
- polling-based single-slot command engine;
- command-list, received-FIS, command-table and one-entry PRDT DMA allocation;
- `IDENTIFY DEVICE`;
- parsing sector count, logical sector size and model string;
- `READ DMA EXT` for single sectors;
- `WRITE DMA EXT` for single sectors;
- `FLUSH CACHE EXT`;
- AHCI-backed read-write `aurora_block_device` registration;
- block-layer LBA0 read verification;
- signed reversible CI write/flush/readback/restore probe restricted to a dedicated signature in the final sector;
- partition discovery through the common partition manager;
- FAT32 and exFAT discovery and read-only access through the common filesystem framework;
- AuroraFS bootstrap format/mount/read path through AHCI;
- mounted-path routing through the common VFS.

### Runtime verification

Workflow **#340** (`36966185493`) is green and runtime-verifies PCI Memory Space / Bus Master enable, ABAR MMIO mapping, AHCI mode enable and SATA-port enumeration on QEMU q35.

Workflow **#344** (`36969225148`) is green and runtime-verifies the first real AHCI DMA command: `IDENTIFY DEVICE` through the polling command engine.

Workflow **#347** (`36969765926`) is green and runtime-verifies `READ DMA EXT`, registration of `ahci-sata0` in the generic block-device registry, and an LBA0 read through `block_device_read()`.

Workflow **#354** (`36971048569`) is green and runtime-verifies the signed reversible `WRITE DMA EXT + FLUSH CACHE EXT + readback + restore` probe on the dedicated QEMU q35 test disk. The log confirms the original final-sector contents are restored before success is reported.

Workflow **Aurora AHCI Filesystem End-to-End #2** (`36972045000`) is green and runtime-verifies the complete storage traversal on a q35 AHCI-backed disk: AHCI block device -> MBR partition discovery -> AuroraFS bootstrap format/mount/read -> FAT32 detection and Unicode VFAT file read -> exFAT detection and file read -> mount manager -> VFS. The same run reaches the M1 user-space bootstrap successfully with ATA PIO unavailable, demonstrating that the verified filesystem traversal is actually using AHCI.

### Not implemented yet

- multi-sector batching and multi-entry PRDT;
- robust timeout/error recovery and port reset;
- NCQ;
- interrupt-driven completion;
- hot-plug handling.

These are performance, concurrency and resilience improvements. They are no longer prerequisites for the verified baseline AHCI storage path.

## DMA memory requirements

AHCI command structures use DMA-visible physical pages owned by the PMM. Their physical addresses are programmed directly into the controller. Command list, received FIS, command tables and data buffers remain valid until command completion.

The current implementation intentionally uses polling, one command slot and one PRDT entry to establish correctness before adding concurrency and interrupt-driven operation.

## Baseline data-I/O gate

The baseline AHCI runtime path now provides:

1. controller initialization — **verified**;
2. one active SATA port — **verified**;
3. `IDENTIFY DEVICE` — **verified**;
4. sector count/logical sector discovery — **verified**;
5. sector reads through the generic block layer — **verified**;
6. signed reversible write/readback probe on the dedicated CI disk — **verified**;
7. explicit cache flush — **verified**;
8. read-write generic `block_device` — **verified**;
9. partition discovery through the existing partition manager — **verified**;
10. filesystem access through the existing mount/VFS stack — **verified**.

ATA PIO remains a compatibility transport. AHCI is now the primary runtime-verified modern SATA transport baseline for Aurora OS.

## Safety and compatibility rules

- Do not assume a fixed logical sector size beyond what `IDENTIFY DEVICE` reports.
- Do not expose an AHCI disk to the block registry until IDENTIFY succeeds and capacity is known.
- Failed commands must not silently fall back to success.
- DMA buffers and command structures must remain valid until the controller has completed the command.
- Filesystem code must remain transport-agnostic.
- Destructive write probes are restricted to media carrying the dedicated `AURORA-AHCI-RW-TEST-V1` signature and restore the original sector contents before reporting success.

## Next implementation gate

The baseline AHCI transport gate is closed. Subsequent AHCI work should focus on robustness and performance: multi-sector commands, larger/multiple PRDT entries, timeout/error recovery, port reset, interrupt-driven completion, NCQ and hot-plug support. The broader storage roadmap can now advance to NVMe and production AuroraFS work without relying on ATA PIO as the primary modern-disk path.
