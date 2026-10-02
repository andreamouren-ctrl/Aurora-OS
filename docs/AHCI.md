# Aurora OS AHCI Transport

Status: **active implementation**

This document defines the current AHCI storage transport state and the implementation contract for modern SATA data I/O.

## Architecture role

AHCI is a hardware transport beneath the generic Aurora block-device layer:

`PCI -> AHCI controller -> SATA port -> AHCI block device -> partition manager -> filesystem -> VFS`

Filesystem support must remain independent from the transport. FAT32, exFAT and AuroraFS must not contain AHCI-specific behavior.

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
- read-only AHCI-backed `aurora_block_device` registration;
- block-layer LBA0 read verification.

### Runtime verification

Workflow **#340** (`36966185493`) is green and runtime-verifies PCI Memory Space / Bus Master enable, ABAR MMIO mapping, AHCI mode enable and SATA-port enumeration on QEMU q35.

Workflow **#344** (`36969225148`) is green and runtime-verifies the first real AHCI DMA command: `IDENTIFY DEVICE` through the polling command engine.

Workflow **#347** (`36969765926`) is green and runtime-verifies `READ DMA EXT`, registration of `ahci-sata0` in the generic block-device registry, and an LBA0 read through `block_device_read()`.

### Not implemented yet

- `WRITE DMA EXT`;
- `FLUSH CACHE EXT`;
- AHCI write/readback durability probe;
- multi-sector batching and multi-entry PRDT;
- robust timeout/error recovery and port reset;
- NCQ;
- interrupt-driven completion;
- hot-plug handling;
- end-to-end partition/filesystem traversal using the AHCI device instead of the ATA PIO compatibility path.

## DMA memory requirements

AHCI command structures use DMA-visible physical pages owned by the PMM. Their physical addresses are programmed directly into the controller. Command list, received FIS, command tables and data buffers remain valid until command completion.

The current implementation intentionally uses polling, one command slot and one PRDT entry to establish correctness before adding concurrency and interrupt-driven operation.

## Initial data-I/O target

The first complete AHCI runtime path must provide:

1. controller initialization — **verified**;
2. one active SATA port — **verified**;
3. `IDENTIFY DEVICE` — **verified**;
4. sector count/logical sector discovery — **verified**;
5. sector reads through the generic block layer — **verified**;
6. write/readback probe on the dedicated CI disk — pending;
7. explicit cache flush — pending;
8. read-write generic `block_device` — pending;
9. partition discovery through the existing partition manager — pending;
10. filesystem access through the existing mount/VFS stack — pending.

ATA PIO remains the compatibility baseline until AHCI read/write/flush and end-to-end filesystem traversal are independently green in CI.

## Safety and compatibility rules

- Do not replace ATA PIO as the CI baseline until AHCI read/write/flush is independently verified.
- Do not assume a fixed logical sector size beyond what `IDENTIFY DEVICE` reports.
- Do not expose an AHCI disk to the block registry until IDENTIFY succeeds and capacity is known.
- Failed commands must not silently fall back to success.
- DMA buffers and command structures must remain valid until the controller has completed the command.
- Filesystem code must remain transport-agnostic.
- Destructive write probes must be limited to dedicated CI media and must preserve/restore the original sector contents.

## Next implementation gate

The next gate is `WRITE DMA EXT + FLUSH CACHE EXT`, followed by a reversible write/flush/readback/restore probe on the dedicated QEMU q35 test disk. Only after that passes will the AHCI block device be treated as read-write and exercised through partition discovery and the filesystem/VFS stack.
