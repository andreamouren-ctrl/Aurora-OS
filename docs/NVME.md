# Aurora OS NVMe Transport

Status: **baseline runtime-verified; advanced implementation continues**

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
- Admin `Create I/O Completion Queue` and `Create I/O Submission Queue`;
- polling I/O queue pair for namespace 1;
- one-logical-block NVM Read and NVM Write commands;
- NVM Flush command;
- read/write `nvme-ns1` registration through the generic block-device layer;
- LBA0 access through `block_device_read()`;
- signed reversible write/flush/readback/restore probe restricted to the final logical block of a dedicated CI image;
- external CI check that the original signature is restored in the backing image after the kernel probe;
- reuse of the common transport-independent storage bootstrap pipeline for NVMe-backed AuroraFS, partition discovery, foreign filesystems and VFS;
- dedicated NVMe filesystem end-to-end CI workflow.

### Runtime verification

Workflow **Aurora NVMe Probe #2** (`36973751130`) is green and runtime-verifies PCI discovery, 64-bit BAR0 decoding, non-cacheable MMIO mapping and reads of `CAP`, `VS` and `CSTS` on QEMU q35 with an emulated NVMe controller.

Workflow **Aurora NVMe Probe #7** (`36974618553`) is green and runtime-verifies bounded controller reconfiguration, Admin Submission/Completion Queues, `Identify Controller`, `Identify Namespace 1`, model/serial parsing, namespace capacity and active logical-block geometry.

Workflow **Aurora NVMe Probe #10** (`36974935129`) is green and runtime-verifies Admin creation of I/O CQ/SQ queue pair 1, NVM Read, registration of `nvme-ns1`, and LBA0 access through the generic block layer.

Workflow **Aurora NVMe Probe #14** (`36975450797`) is green and runtime-verifies NVM Write, NVM Flush, readback and restoration through the read/write `nvme-ns1` block device. The CI also compares the final backing-image sector after QEMU exits to confirm that the original `AURORA-NVME-RW-TEST-V1` signature was restored.

Workflow **Aurora NVMe Filesystem End-to-End #1** (`36975832885`) is green and runtime-verifies the complete baseline path `NVMe -> block layer -> MBR -> AuroraFS/FAT32/VFAT/exFAT -> mount manager -> VFS`. In that run the AHCI controller reports zero active SATA ports and ATA PIO is unavailable, so the filesystem traversal is demonstrably carried by `nvme-ns1`. The run verifies AuroraFS formatting/mounting and `/system` routing, two MBR partitions, FAT32 file access, VFAT Unicode long filename access, exFAT file access and M1 bootstrap completion.

The verified CI controller reports NVMe version `1.4.0`, 2048 maximum queue entries, doorbell stride 0, and supported controller page shifts 12 through 16.

### Not implemented yet

The baseline NVMe storage path is complete. Remaining work is advanced robustness and performance:

- interrupt/MSI-X completion instead of polling;
- stronger controller/queue timeout and reset recovery;
- multiple controllers and multiple namespaces;
- batching and larger transfer/PRP-list handling;
- hot-plug policy where the platform supports it;
- physical-hardware validation beyond QEMU emulation.

## Safety and correctness rules

- Never assume BAR0 is 32-bit; NVMe commonly exposes a 64-bit MMIO BAR.
- Do not expose a namespace as a block device until `Identify Namespace` establishes capacity and logical-block geometry.
- DMA queue memory must be page-aligned, physically addressable and remain valid until the controller completes the command.
- Controller state transitions must honor `CAP.TO`, `CC.EN` and `CSTS.RDY` rather than using unbounded waits.
- Queue depth must not exceed the controller's `CAP.MQES` limit.
- Doorbell addresses must respect `CAP.DSTRD`.
- Automatic destructive verification is forbidden on arbitrary devices. The bootstrap write probe executes only when the final logical block contains the exact dedicated CI signature and restores that block before reporting success.
- Filesystem code must remain independent of NVMe.

## Next development direction

NVMe no longer blocks basic storage functionality. The main storage-development gate now moves to AuroraFS: evolve bootstrap v1 toward scalable allocation, directories and multi-block/extented files while preserving compatibility and adding explicit migration/versioning rules. Advanced NVMe work can proceed after the native filesystem foundation is stronger.
