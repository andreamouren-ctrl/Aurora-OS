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
- identifying active SATA ATA-device ports.

### Runtime verification

The PCI/ABAR discovery path was already runtime-verified by the existing q35 BIOS smoke test.

The new MMIO mapping, AHCI-mode enable and SATA-port enumeration code is **implemented but not yet considered runtime-verified** until a post-change CI run completes green.

### Not implemented yet

- command-list allocation;
- received-FIS allocation;
- command-table allocation;
- PRDT construction;
- DMA buffer management;
- SATA IDENTIFY DEVICE command;
- READ DMA EXT;
- WRITE DMA EXT;
- FLUSH CACHE EXT;
- timeout/error recovery;
- NCQ;
- interrupt-driven completion;
- hot-plug handling;
- AHCI-backed `aurora_block_device` registration.

## DMA memory requirements

AHCI command structures require DMA-visible physical memory. Aurora must use physical pages owned by the PMM and expose their physical addresses directly to the controller. Command list, received FIS, command tables and data buffers must satisfy AHCI alignment requirements and must not rely on arbitrary kernel virtual addresses as DMA addresses.

Initial implementation may use polling and a single command slot to establish correctness. Interrupt-driven completion and multiple outstanding commands are later stages.

## Initial data-I/O target

The first complete AHCI runtime path must provide:

1. controller initialization;
2. one active SATA port;
3. `IDENTIFY DEVICE`;
4. sector count discovery;
5. read of one or more sectors;
6. write/readback probe on a signed CI disk;
7. explicit cache flush;
8. registration as a generic `block_device`;
9. partition discovery through the existing partition manager;
10. filesystem access through the existing mount/VFS stack.

ATA PIO remains the compatibility baseline until this entire path is green in CI.

## Safety and compatibility rules

- Do not replace ATA PIO as the CI baseline until AHCI read/write/flush is independently verified.
- Do not assume a fixed physical sector size beyond what the device reports.
- Do not expose an AHCI disk to the block registry until IDENTIFY succeeds and capacity is known.
- Failed commands must not silently fall back to success.
- DMA buffers and command structures must remain valid until the controller has completed the command.
- Filesystem code must remain transport-agnostic.

## Next implementation gate

The next gate is a minimal polling-based AHCI command engine using one command slot and one PRDT entry, beginning with `IDENTIFY DEVICE`, followed by read-only sector I/O. Write and flush support are enabled only after read-path verification.
