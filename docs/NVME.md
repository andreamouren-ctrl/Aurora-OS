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
- bounded controller disable/enable through `CC.EN` / `CSTS.RDY` using `CAP.TO`;
- page-aligned PMM-backed Admin Submission and Completion Queue memory;
- `AQA`, `ASQ`, `ACQ` programming;
- queue doorbell addressing using `CAP.DSTRD`;
- command identifiers and Completion Queue phase handling;
- Admin `Identify Controller`;
- Admin `Identify Namespace` for namespace 1;
- parsing of controller model/serial, namespace count, capacity and active logical-block size;
- bootstrap diagnostics for controller, queue and namespace geometry;
- dedicated QEMU q35 NVMe CI workflow with explicit Admin Identify assertions.

### Runtime verification

Workflow **Aurora NVMe Probe #2** (`36973751130`) is green and runtime-verifies PCI discovery, 64-bit BAR0 decoding, non-cacheable MMIO mapping and reads of `CAP`, `VS` and `CSTS` on QEMU q35 with a real emulated NVMe controller.

The verified CI controller reports NVMe version `1.4.0`, 2048 maximum queue entries, doorbell stride 0, supported controller page shifts 12 through 16, and reaches the M1 user-space bootstrap after the probe.

Admin Queue initialization and Identify Controller/Namespace are implemented but remain **pending runtime verification** until the updated NVMe CI gate passes.

### Not implemented yet

- I/O Submission/Completion Queue pairs;
- NVM read/write commands;
- Flush command;
- generic `aurora_block_device` registration;
- partition/filesystem/VFS traversal through NVMe;
- interrupts/MSI-X;
- robust timeout/recovery/reset policy beyond bounded bootstrap controller transitions;
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

After the Admin Queue + Identify gate is runtime-verified, the next gate is:

1. issue Admin `Create I/O Completion Queue` and `Create I/O Submission Queue`;
2. create a polling I/O queue pair for namespace 1;
3. implement one-logical-block NVMe Read;
4. expose namespace 1 initially as a read-only `aurora_block_device`;
5. runtime-verify LBA0 through the generic block layer;
6. only then add NVMe Write + Flush and a signed reversible CI write probe.

After read/write/flush is stable, Aurora can run the partition manager and filesystem/VFS traversal over the NVMe namespace.
