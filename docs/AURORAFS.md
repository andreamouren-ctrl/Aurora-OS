# AuroraFS

Status: **bootstrap v1 runtime-verified; v2 production-layout foundation in active development**.

AuroraFS is Aurora OS's native persistent filesystem direction. Bootstrap v1 remains the currently mounted native format and is preserved for compatibility. Production development proceeds as a separately versioned v2 format rather than mutating the v1 on-disk contract in place.

## Verified bootstrap v1 status

AuroraFS bootstrap v1 is runtime-verified in QEMU across ATA PIO, AHCI and NVMe paths. It is registered through the common filesystem-driver registry, mounted at `/system`, and `aurora.boot-probe` is read through the common VFS path. Synthetic 4096-byte logical-block format/create/reopen/mount/read behavior is also runtime-verified while preserving the v1 512-byte logical filesystem block layout.

The modern transport checks include **Aurora AHCI Filesystem End-to-End #2** (`36972045000`) and **Aurora NVMe Filesystem End-to-End #1** (`36975832885`). These are QEMU runtime checks, not physical-hardware certification.

## Versioning policy

AuroraFS formats are explicit and never silently reinterpreted:

- **v1** magic: `AURAFS1\0`, version `1`, 512-byte logical filesystem blocks, bootstrap-only limits;
- **v2** magic: `AURAFS2\0`, version `2`, production-layout foundation with 4096-byte logical filesystem blocks and 64-bit metadata fields.

The v1 reader/writer remains intact while v2 is developed and tested. No v1 volume is automatically upgraded. A future migration tool must perform an explicit, recoverable conversion once v2 is mature enough for general use.

## Design goals

- Aurora-native on-disk format, not a FAT/ext clone.
- Strict separation from hardware transports and the VFS layer.
- Explicit versioning from the first persistent format.
- Metadata and data integrity checks.
- No automatic formatting of arbitrary disks.
- 64-bit sizes, block addresses, object identifiers and offsets in production structures.
- Scalable free-space management rather than fixed bootstrap arrays.
- Multi-block files and extents as baseline production capabilities.
- Directories, sparse files and multi-extent files as first-class requirements.
- Crash-consistent metadata updates before production write support is considered stable.
- On-disk layout independent of SATA, NVMe, USB mass storage, virtual disks and future transports.

## Scale contract

"Unlimited" storage is not physically or mathematically possible. Aurora therefore defines the production goal as *practically unbounded for contemporary and foreseeable systems* by preserving 64-bit widths throughout the storage stack.

No production API or v2 metadata structure may inherit bootstrap v1 limits such as eight directory entries or one 512-byte block per file.

## AuroraFS v2 production-layout foundation

The first v2 milestone deliberately focuses on durable structural primitives before the common driver is switched to v2.

### Logical block size

AuroraFS v2 uses a **4096-byte logical filesystem block**. The filesystem block size is independent of the underlying device logical-block size. The implementation must translate safely over supported 512/1024/2048/4096-byte block devices through the common block layer.

### Superblock

The v2 superblock records at minimum:

- magic and explicit format version;
- filesystem block size;
- total filesystem block count;
- generation;
- allocation-bitmap start and length;
- inode-table start and length;
- first data block;
- root object identifier;
- next object identifier;
- feature/compatibility flags;
- metadata checksum.

The initial v2 metadata checksum is CRC32. The checksum field is zeroed while calculating the checksum.

### Allocation bitmap

Free/used filesystem blocks are tracked by an on-disk bitmap whose length is calculated from the volume size. The bitmap is not a fixed bootstrap array. Metadata blocks are marked allocated during format and data allocation searches the bitmap for free ranges.

This is a foundation for scalable allocation. Future work may add allocation groups or trees for very large volumes without changing the semantic allocation contract.

### Inodes

The initial v2 inode record is fixed at 256 bytes and uses 64-bit fields for:

- object identifier;
- parent object identifier;
- logical file size;
- allocated byte count;
- metadata generation;
- timestamps when that milestone is enabled;
- extent block addresses and lengths.

The first milestone supports inline extent descriptors sufficient to prove multi-block persistence. The on-disk inode also reserves an extent-tree/root pointer so the format can grow beyond inline extents without reintroducing a fixed file-size ceiling.

### Extents

An extent identifies a contiguous run of filesystem blocks with 64-bit logical and physical block coordinates. The first v2 runtime test must create and reopen a file larger than one 4 KiB filesystem block. Passing that test proves that AuroraFS is no longer structurally tied to the v1 single-block-file limit.

### Directory direction

The v2 root object is represented by an inode rather than by the v1 fixed eight-entry table. General directory indexing is a subsequent milestone. The production design must support dynamically growing directories and nested directory objects; it must not freeze a fixed number of names into the superblock or root block.

## v2 safety gates

During early development AuroraFS v2 is **self-test only** and is not automatically selected for arbitrary disks. The v1 common driver remains the mounted format until v2 has runtime verification for format/reopen, allocation integrity, multi-block files, directories and crash-consistency behavior.

A v2 formatter must never overwrite an unknown filesystem merely because AuroraFS probing failed.

## Bootstrap v1 layout

The v1 bootstrap filesystem begins at device LBA 8 so low sectors remain available to development storage probes and partition metadata. Relative to the AuroraFS base:

| Relative block | Purpose |
| --- | --- |
| 0 | Superblock |
| 1 | Fixed root directory table |
| 2..9 | Bootstrap data blocks |

All v1 filesystem blocks are 512 bytes. The root contains eight fixed 64-byte entries and each file is limited to one 512-byte block. These are deliberately retained only as compatibility/bootstrap constraints.

## Current v1 common filesystem integration

The v1 driver implements:

- format probing;
- mount;
- root-directory `stat`;
- root-directory enumeration;
- file reads;
- writes to existing bootstrap files;
- common VFS routing through `/system`;
- device-block translation across 512/1024/2048/4096-byte block devices.

It remains suitable for bootstrap persistence validation but is not a production filesystem.

## Remaining production milestones

After the initial v2 allocation/inode/extent self-test, production work proceeds through:

1. v2 directory records and nested directory traversal;
2. multiple extents and extent-tree overflow;
3. general create/truncate/remove/rename operations;
4. sparse-file semantics;
5. transactional or copy-on-write metadata update strategy;
6. durable free-space reclamation;
7. permissions, ownership, ACLs and timestamps;
8. corruption detection and recovery structures;
9. explicit v1-to-v2 migration tooling;
10. common filesystem-driver/VFS activation only after persistence and corruption-focused CI gates are green.

Encryption and snapshots remain later features and are not prerequisites for the first production-layout milestone.
