# AuroraFS

Status: **bootstrap v1 runtime-verified; v2 layout, nested directories, multi-block allocator and scalable formatter runtime-verified; extent-tree overflow implemented and awaiting runtime promotion**.

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

Aurora defines the production goal as practically unbounded for contemporary and foreseeable systems by preserving 64-bit widths throughout the storage stack. No production API or v2 metadata structure may inherit bootstrap v1 limits such as eight directory entries or one 512-byte block per file.

## AuroraFS v2 production-layout foundation

AuroraFS v2 uses a **4096-byte logical filesystem block** independent of the underlying device logical-block size. The initial runtime gate verifies the same v2 layout over synthetic 512-byte and 4096-byte block devices.

The v2 superblock records magic/version, filesystem block size, total blocks, generation, allocation-bitmap and inode-table geometry, first data block, root/next object identifiers, feature flags and CRC32 metadata checksum.

The initial v2 inode is 256 bytes and uses 64-bit object IDs, parent IDs, file sizes, allocated byte counts, generation and extent coordinates. Four inline extents provide the fast path while the extent-tree root field is the overflow path for fragmented files.

The first runtime-verified file test persists a **6000-byte file across two 4 KiB filesystem blocks**, reopens it, validates bitmap/inode/extent metadata and verifies the complete byte pattern.

## v2 multi-block bitmap allocator milestone

The one-bitmap-block limitation has been removed from the allocator layer. `aurora_fs_v2_allocator` traverses bitmap storage in 4 KiB windows and therefore does not need to keep the full free-space map in RAM. It supports 64-bit filesystem-block coordinates, contiguous range allocation, allocated-state queries and range release through the common block-device API.

The dedicated synthetic test models a 70,000-block v2 filesystem requiring three bitmap blocks. It forces allocation into the second bitmap block, verifies persistence after reopen, frees the range, and separately allocates a run spanning the bitmap 0 -> 1 boundary. The same test runs over devices exposing 512-byte and 4096-byte logical blocks.

Build workflow **#407** (`36980285923`) is green and the serial log explicitly reports:

`[aurorafs-v2] multi-block bitmap + cross-boundary range allocator self-test passed on 512/4096-byte devices`

Therefore the scalable allocator is **runtime-verified in the synthetic QEMU boot environment**.

## v2 scalable formatter milestone

A reusable `aurora_fs_v2_format_device()` path computes volume geometry from the block device, supports bitmap lengths derived from the 64-bit volume geometry, writes the checksummed v2 superblock, initializes every bitmap block, reserves metadata and initial data blocks, zeroes the inode-table region and flushes through the common block-device contract.

Its self-test models a 70,000-block volume requiring three bitmap blocks, reopens the written superblock, validates checksum and geometry, initializes the general allocator from the formatted volume and verifies that the first post-format allocation begins exactly after the formatter-reserved data range. The gate runs on synthetic 512-byte and 4096-byte block devices.

Build workflow **#411** (`36981472946`) is green and the serial log explicitly reports:

`[aurorafs-v2] scalable multi-bitmap formatter + reopen/allocator integration self-test passed on 512/4096-byte devices`

The formatter is therefore **runtime-verified in the synthetic QEMU boot environment**.

## v2 extent-tree overflow milestone

The v2 overflow path now has a dedicated extent-tree module rather than increasing the fixed inode extent array. The public extent mapping remains 64-bit (`logical_block`, `physical_block`, `block_count`). Tree nodes are one 4 KiB filesystem block, contain a 64-byte checksummed header and 32-byte entries, and are allocated through the already verified general bitmap allocator.

A leaf node stores up to **126 extent mappings**. When a file exceeds one leaf, a level-1 root stores logical ranges and child-block pointers. The current first implementation therefore represents up to **15,876 extents** with one root plus up to 126 leaves while keeping lookup bounded to one root read and one leaf read. Node CRC32 is verified on reopen.

The new self-test deliberately creates **130 one-block file extents separated by allocated blocks**, forcing physical fragmentation and forcing the tree to overflow from the first leaf into a second leaf. It writes the tree, reopens the allocator, resolves every logical block to the expected physical block and explicitly probes both sides of the leaf boundary. The same gate runs on synthetic 512-byte and 4096-byte devices.

This extent-tree code is **implemented and boot-gated but not yet runtime-promoted** until its CI run is green and the explicit success serial line is observed:

`[aurorafs-v2] two-level extent tree + 130 fragmented extents persistence self-test passed on 512/4096-byte devices`

Current limitations of this first tree milestone: no level-2+ root growth, no node split/merge update algorithm, and no rollback yet if a multi-node construction fails after some blocks have been allocated. Those are later mutation/crash-consistency hardening tasks, not claims of the current gate.

## v2 directory-record milestone

Directory storage is no longer modeled as the v1 fixed root table. The v2 directory milestone implements a **128-byte checksummed directory record** containing object ID, node type, name length and UTF-8 name bytes.

The self-test deliberately gives the root directory a two-block extent and places the `docs` record in the second block. After reopening the synthetic volume, lookup scans beyond the first 4 KiB directory block, resolves the `docs` inode, then resolves `docs/note.txt` from the nested directory inode and verifies the persisted file payload.

Build workflow **#399** (`36977213539`) is green and the serial log explicitly reports:

`[aurorafs-v2] dynamic two-block root + nested directory traversal self-test passed on 512/4096-byte devices`

## v2 runtime verification

Build workflow **#394** (`36976628830`) runtime-verifies the first layout foundation.

Build workflow **#399** (`36977213539`) runtime-verifies dynamic nested-directory traversal.

Build workflow **#407** (`36980285923`) runtime-verifies multi-block bitmap traversal and the general cross-boundary range allocator.

Build workflow **#411** (`36981472946`) runtime-verifies scalable multi-bitmap formatting plus reopen/allocator integration.

The two-level extent-tree overflow path is implemented after #411 and remains pending promotion until its newer CI gate completes.

## v2 safety gates

AuroraFS v2 remains **self-test only** and is not automatically selected for arbitrary disks. The v1 common driver remains the mounted format until v2 has runtime verification for broader file operations and crash-consistency behavior.

A v2 formatter must never overwrite an unknown filesystem merely because AuroraFS probing failed.

## Bootstrap v1 layout

The v1 bootstrap filesystem begins at device LBA 8. Relative to the AuroraFS base:

| Relative block | Purpose |
| --- | --- |
| 0 | Superblock |
| 1 | Fixed root directory table |
| 2..9 | Bootstrap data blocks |

All v1 filesystem blocks are 512 bytes. The root contains eight fixed 64-byte entries and each file is limited to one 512-byte block. These are retained only as compatibility/bootstrap constraints.

## Current v1 common filesystem integration

The v1 driver implements format probing, mount, root `stat`/enumeration, file reads, writes to existing bootstrap files, `/system` VFS routing, and device-block translation across 512/1024/2048/4096-byte block devices.

It remains suitable for bootstrap persistence validation but is not a production filesystem.

## Remaining production milestones

1. runtime-verify the two-level extent-tree overflow path;
2. integrate inline-to-tree extent promotion into persistent v2 inode/file mutation;
3. general create/truncate/remove/rename operations;
4. sparse-file semantics;
5. transactional or copy-on-write metadata update strategy;
6. durable free-space reclamation and failed-operation rollback;
7. permissions, ownership, ACLs and timestamps;
8. corruption detection and recovery structures;
9. explicit v1-to-v2 migration tooling;
10. common filesystem-driver/VFS activation only after persistence and corruption-focused CI gates are green.

Encryption and snapshots remain later features and are not prerequisites for the first production-layout milestones.
