# AuroraFS

Status: **bootstrap v1 runtime-verified; v2 production-layout foundation runtime-verified, directory work active**.

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

The first v2 milestone focuses on durable structural primitives before the common driver is switched to v2.

### Logical block size

AuroraFS v2 uses a **4096-byte logical filesystem block**. The filesystem block size is independent of the underlying device logical-block size. The initial runtime gate verifies the same v2 layout over synthetic 512-byte and 4096-byte block devices.

### Superblock

The v2 superblock records:

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

Free/used filesystem blocks are represented by an on-disk allocation bitmap and metadata/data blocks are marked allocated during format. The current first implementation calculates bitmap length from the volume, but its formatter/self-test currently accepts only layouts whose bitmap occupies one 4 KiB filesystem block. **This is a deliberate first-milestone limit and must not be described as fully scalable allocation yet.** Multi-block bitmap traversal/allocation is a required follow-up before large-volume scalability is claimed.

### Inodes

The initial v2 inode record is fixed at 256 bytes and uses 64-bit fields for:

- object identifier;
- parent object identifier;
- logical file size;
- allocated byte count;
- metadata generation;
- extent-tree root reservation;
- extent logical/physical coordinates and lengths.

The first milestone supports inline extent descriptors sufficient to prove multi-block persistence. The on-disk inode reserves an extent-tree/root pointer so the format can grow beyond inline extents without reintroducing a fixed file-size ceiling.

### Extents

An extent identifies a contiguous run of filesystem blocks with 64-bit logical and physical block coordinates. The current runtime self-test formats a v2 synthetic volume, persists a **6000-byte file across two 4 KiB filesystem blocks**, reopens the metadata, validates the allocation bitmap and inode/extent mapping, and verifies the full byte pattern after reopen.

### Directory direction

The v2 root object is represented by an inode rather than by the v1 fixed eight-entry table. General directory indexing is the active next milestone. The production design must support dynamically growing directories and nested directory objects; it must not freeze a fixed number of names into the superblock or root block.

## v2 runtime verification

Build workflow **#394** (`36976628830`, head `9e5e2c72fd65d4e4d5ede9066ed066d5a1455d7c`) is green. Its BIOS serial log explicitly reports:

`[aurorafs-v2] 4KiB layout + bitmap + 64-bit inode + multi-block extent self-test passed on 512/4096-byte devices`

The kernel subsequently reaches M1, so the first v2 layout foundation is **runtime-verified in the synthetic QEMU boot environment**. This verification covers the v2 formatter/reopen path, CRC32 superblock validation, one-block allocation bitmap behavior, 256-byte 64-bit inode metadata, a contiguous two-block extent, and multi-block data persistence on synthetic 512-byte and 4096-byte devices.

It does **not** yet verify dynamic directories, multiple extents, multi-block allocation bitmaps, crash consistency, migration, the common filesystem-driver/VFS v2 path, or physical hardware.

## v2 safety gates

AuroraFS v2 remains **self-test only** and is not automatically selected for arbitrary disks. The v1 common driver remains the mounted format until v2 has runtime verification for directory operations, broader allocation behavior, general file operations and crash-consistency behavior.

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

After the runtime-verified first v2 allocation/inode/extent foundation, production work proceeds through:

1. v2 dynamic directory records and nested directory traversal;
2. multi-block allocation bitmap support and general free-range allocation;
3. multiple extents and extent-tree overflow;
4. general create/truncate/remove/rename operations;
5. sparse-file semantics;
6. transactional or copy-on-write metadata update strategy;
7. durable free-space reclamation;
8. permissions, ownership, ACLs and timestamps;
9. corruption detection and recovery structures;
10. explicit v1-to-v2 migration tooling;
11. common filesystem-driver/VFS activation only after persistence and corruption-focused CI gates are green.

Encryption and snapshots remain later features and are not prerequisites for the first production-layout milestones.
