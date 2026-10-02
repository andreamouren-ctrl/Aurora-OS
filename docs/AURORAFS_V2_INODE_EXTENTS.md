# AuroraFS v2 — Persistent inode extent promotion

Status: **two-level extent tree runtime-verified; persistent inline-to-tree inode promotion implemented and awaiting runtime promotion**.

This document is part of the AuroraFS v2 on-disk contract. It distinguishes code that exists in the repository from behavior demonstrated by a runtime CI gate.

## Verified prerequisite: two-level extent tree

Aurora OS Bootstrap Build **#414** (`36981973396`, head `115936948dd0e9426fca2f3323e497bd7e125642`) completed successfully. Its QEMU serial log explicitly reports:

`[aurorafs-v2] two-level extent tree + 130 fragmented extents persistence self-test passed on 512/4096-byte devices`

The line is present in the q35/AHCI boot and again in the ATA PIO boot. Therefore the current two-level AuroraFS v2 extent-tree foundation is **runtime-verified in the synthetic QEMU environment** for devices exposing 512-byte and 4096-byte logical blocks. This is not a physical-hardware 4Kn certification.

The verified tree uses 4 KiB checksummed nodes. A leaf stores up to 126 extent mappings. A level-1 root stores child logical ranges and child filesystem-block pointers, allowing the current two-level representation to describe up to 15,876 extents. Higher tree levels and online node split/merge are not implemented yet.

## Persistent inode promotion contract

AuroraFS v2 inodes remain **256 bytes** and retain the already established four-inline-extent layout. The new persistent mutation primitive does not silently change that on-disk inode size or reorder existing fields.

A regular file begins with `extent_tree_root == 0`. The first four appended extents are persisted directly in the inode's four inline slots. When a fifth extent is appended, AuroraFS v2 now:

1. reads the current inode from the formatter-defined inode table;
2. validates the existing inline extent sequence and the new extent;
3. builds an extent tree containing the four existing mappings plus the new fifth mapping;
4. persists and flushes the new tree through the common block-device and allocator layers;
5. stores the resulting root filesystem-block coordinate in `extent_tree_root`;
6. records the total extent count as five;
7. clears the no-longer-authoritative inline slots;
8. advances inode generation, size and allocated-byte metadata;
9. persists and flushes the updated inode block.

Lookup is unified: an inode with no tree root is resolved from its inline extents; an inode with a nonzero tree root delegates to the verified extent-tree lookup path. Callers therefore do not need separate read logic after promotion.

## Repository implementation

The public v2 interface now exposes persistent inode extent initialization, append and lookup primitives in `kernel/include/aurora/aurora_fs_v2.h`. The implementation is isolated in `kernel/src/fs/aurora_fs_v2_inode_extents.c` and operates through `aurora_block_device`, `aurora_fs_v2_format_geometry`, `aurora_fs_v2_allocator`, and the extent-tree API.

The boot self-test formats a fresh v2 volume, creates a regular-file inode, allocates five deliberately separated one-block data ranges, persists four inline extents, appends the fifth extent to force promotion, reopens the allocator and resolves every logical extent again. The same test is executed over synthetic 512-byte and 4096-byte logical block devices.

Expected runtime gate:

`[aurorafs-v2] persistent inode inline-to-tree promotion at fifth extent self-test passed on 512/4096-byte devices`

Until a green CI run contains that exact serial line, this promotion path remains **implemented but not runtime-verified**.

## Safety and current limits

The promotion sequence deliberately writes the new tree before switching the inode root, so an inode never points to a tree that has not been written. However, the operation is **not yet crash-transactional**: if inode persistence fails after tree allocation, tree blocks can be leaked. This is an accepted development-stage limitation and must be addressed by the planned transactional/copy-on-write metadata strategy and recovery model.

Appending additional extents to an inode that has already been promoted is not yet supported by this first mutation milestone. Online tree mutation, node splitting/merging, tree-block reclamation, truncate/remove integration and rollback are follow-up work.

## Next gate

After runtime promotion of the fifth-extent inode transition, the next AuroraFS v2 storage milestone is to support **continued append/mutation after promotion**, including rebuilding or mutating the existing tree safely. That work is a prerequisite for general create/truncate/remove/rename operations and for production fragmented-file growth.
