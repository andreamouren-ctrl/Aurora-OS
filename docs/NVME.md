# Aurora OS NVMe Transport

Status: **active implementation**

This document defines the NVMe transport implementation contract and distinguishes repository implementation from runtime verification.

## Architecture role

NVMe is a modern storage transport beneath Aurora's generic block layer:

`PCIe -> NVMe controller -> namespace -> NVMe block device -> partition manager -> filesystem -> VFS`

Filesystem drivers remain transport-agnostic. AuroraFS, FAT32, exFAT and future filesystems must not contain NVMe-specific behavior.

## Current implementation state

### Implemented

- PCI class/subclass/programming-interface discovery for NVMe controllers (`01/08/02`);
- generic PCI 64-bit MMIO BAR reader;
- PCI Memory Space and Bus Master enable through the existing PCI layer;
- BAR0 mapping into a dedicated non-cacheable kernel virtual range;
- NVMe `CAP`, `VS` and `CSTS` register reads;
- extraction of maximum queue entries, doorbell stride and controller page-size range;
- bootstrap diagnostics for controller address, BAR0, version and capability geometry;
- dedicated QEMU q35 NVMe CI workflow.

### Runtime verification

Workflow **Aurora NVMe Probe #2** (`36973751130`) is green and runtime-verifies PCI discovery, 64-bit BAR0 decoding, non-cacheable MMIO mapping and reads of `CAP`, `VS` and `CSTS` on QEMU q35 with a real emulated NVMe controller.

The verified CI controller reports NVMe version `1.4.0`, 2048 maximum queue entries, doorbell stride 0, supported controller page shifts 12 through 16, and reaches the M1 user-space bootstrap after the probe.

This verifies only controller discovery/MMIO capability access. It does **not** yet verify Admin Queues, Identify commands, namespace access or NVM data I/O.

### Not implemented yet

- controller disable/enable transition through `CC.EN` / `CSTS.RDY`;
- Admin Submission Queue and Admin Completion Queue;
- doorbell handling;
- command identifiers and completion phase handling;
- `Identify Controller`;
- namespace discovery and `Identify Namespace`;
- namespace logical-block format parsing;
- I/O Submission/Completion Queue pairs;
- NVM read/write commands;
- Flush command;
- generic `aurora_block_device` registration;
- partition/filesystem/VFS traversal through NVMe;
- interrupts/MSI-X;
- timeout/recovery/reset policy;
- multiple controllers/namespaces and hot-plug policy.

## Safety and correctness rules

- Never assume BAR0 is 32-bit; NVMe commonly exposes a 64-bit MMIO BAR.
- Do not expose a namespace as a block device until `Identify Namespace` establishes capacity and logical-block geometry.
- DMA queue memory must be page-aligned, physically addressable and remain valid until the controller completes the command.
- Controller state transitions must honor `CAP.TO`, `CC.EN` and `CSTS.RDY` rather than using unbounded waits.
- Queue depth must not exceed the controller's `CAP.MQES` limit.
- Doorbell addresses must respect `CAP.DSTRD`.
- Destructive write verification must be restricted to a dedicated signed CI image and restore original data before reporting success.
- Filesystem code must remain independent of NVMe.

## Next implementation gate

The next gate is:

1. allocate page-aligned Admin SQ/CQ DMA memory;
2. disable the controller if required and wait for `CSTS.RDY=0`;
3. program `AQA`, `ASQ`, `ACQ`;
4. configure `CC` for the selected page size and queue entry sizes;
5. enable the controller and wait for `CSTS.RDY=1`;
6. submit `Identify Controller` through Admin Queue 0;
7. discover namespace 1 and submit `Identify Namespace`;
8. parse namespace capacity and active LBA format.

Only after that gate is runtime-verified should Aurora create NVMe I/O queues and register a namespace in the generic block layer.
