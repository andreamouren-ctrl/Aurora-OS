# AuroraFS

Status: **bootstrap v1 runtime-verified; v2 layout and nested directory traversal runtime-verified; multi-block allocation implemented and awaiting runtime promotion**.

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

AuroraFS v2 uses a **4096-byte logical filesystem block** independent of the underlying device logical-block size. The initial runtime gate verifies the same v2 layout over synthetic 512-byte and 4096-byte block devices.

The v2 superblock records magic/version, filesystem block size, total blocks, generation, allocation-bitmap and inode-table geometry, first data block, root/next object identifiers, feature flags and CRC32 metadata checksum.

The initial v2 inode is 256 bytes and uses 64-bit object IDs, parent IDs, file sizes, allocated byte counts, generation and extent coordinates. Inline extents prove multi-block persistence while an extent-tree root field is reserved for later overflow support.

The first runtime-verified file test persists a **6000-byte file across two 4 KiB filesystem blocks**, reopens it, validates bitmap/inode/extent metadata and verifies the complete byte pattern.

## v2 multi-block bitmap allocator milestone

The one-bitmap-block limitation has now been removed from the allocator layer. The new `aurora_fs_v2_allocator` traverses bitmap storage in 4 KiB windows and therefore does not need to keep the full free-space map in RAM. It supports 64-bit filesystem-block coordinates, contiguous range allocation, allocated-state queries and range release through the common block-device API.

The allocator validates that bitmap capacity covers the advertised filesystem geometry. Allocation scans can preserve a free run across a bitmap-block boundary, and bitmap updates touch only the affected bitmap blocks before issuing the block-device flush contract.

The dedicated synthetic test models a 70,000-block v2 filesystem, which requires three bitmap blocks. It deliberately marks the first bitmap region full so the first allocation is forced into bitmap block 1, reopens the allocator to verify persistence, frees that range, and separately allocates a six-block run spanning the bitmap 0 -> 1 boundary. The same test runs over devices exposing 512-byte and 4096-byte logical blocks.

This code is implemented and wired into the boot storage self-test. It remains **pending runtime promotion** until the corresponding CI run is green and the explicit allocator-success serial line is observed.

This milestone does not yet make the main v2 formatter itself capable of formatting arbitrarily large volumes; formatter integration of the scalable allocator remains a follow-up after this primitive is runtime-verified.

## v2 directory-record milestone

Directory storage is no longer modeled as the v1 fixed root table. The v2 directory milestone implements a **128-byte checksummed directory record** containing object ID, node type, name length and UTF-8 name bytes.

The self-test deliberately gives the root directory a two-block extent and places the `docs` record in the second block. After reopening the synthetic volume, lookup scans beyond the first 4 KiB directory block, resolves the `docs` inode, then resolves `docs/note.txt` from the nested directory inode and verifies the persisted file payload.

This directory test runs on synthetic devices exposing both 512-byte and 4096-byte logical blocks. Build workflow **#399** (`36977213539`, head `877131a721264f0992b2c701839d8da0b8b9162d`) is green and the serial log explicitly reports:

`[aurorafs-v2] dynamic two-block root + nested directory traversal self-test passed on 512/4096-byte devices`

The same run reaches the M1 user-space bootstrap and repeats the directory-success line in the ATA storage boot, so persisted nested traversal is **runtime-verified in the synthetic QEMU boot environment**.

This first directory milestone proves persisted nested traversal and directory extents; it does not yet provide the production create/remove/rename API, directory compaction, free-slot reuse or an indexed lookup structure.

## v2 runtime verification

Build workflow **#394** (`36976628830`) runtime-verifies the first layout foundation:

`[aurorafs-v2] 4KiB layout + bitmap + 64-bit inode + multi-block extent self-test passed on 512/4096-byte devices`

Build workflow **#399** (`36977213539`) runtime-verifies the next directory milestone:

`[aurorafs-v2] dynamic two-block root + nested directory traversal self-test passed on 512/4096-byte devices`

Together these gates currently verify formatter/reopen, CRC32 superblock validation, one-block formatter bitmap behavior, 256-byte 64-bit inode metadata, contiguous multi-block extents, multi-block file persistence, a two-block root directory extent, nested directory lookup and persisted nested file reads on synthetic 512-byte and 4096-byte devices.

The scalable multi-block allocator is implemented after #399 but is not counted as runtime-verified until its newer CI gate completes successfully.

## v2 safety gates

AuroraFS v2 remains **self-test only** and is not automatically selected for arbitrary disks. The v1 common driver remains the mounted format until v2 has runtime verification for broader allocation behavior, general file operations and crash-consistency behavior.

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

After the runtime-verified layout and nested-directory milestones, production work proceeds through:

1. runtime-verify the multi-block bitmap/range allocator and integrate it into the v2 formatter;
2. multiple extents and extent-tree overflow;
3. general create/truncate/remove/rename operations;
4. sparse-file semantics;
5. transactional or copy-on-write metadata update strategy;
6. durable free-space reclamation;
7. permissions, ownership, ACLs and timestamps;
8. corruption detection and recovery structures;
9. explicit v1-to-v2 migration tooling;
10. common filesystem-driver/VFS activation only after persistence and corruption-focused CI gates are green.

Encryption and snapshots remain later features and are not prerequisites for the first production-layout milestones.
