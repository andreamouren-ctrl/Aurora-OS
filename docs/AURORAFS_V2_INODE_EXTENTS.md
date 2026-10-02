# AuroraFS v2 — Persistent inode extent promotion

Status: **two-level extent tree and persistent inline-to-tree inode promotion runtime-verified; continued tree-backed append is the active milestone**.

This document is part of the AuroraFS v2 on-disk contract. It distinguishes repository implementation from behavior demonstrated by runtime CI gates.

## Verified prerequisite: two-level extent tree

Aurora OS Bootstrap Build **#414** (`36981973396`, head `115936948dd0e9426fca2f3323e497bd7e125642`) completed successfully. Its QEMU serial log reports:

`[aurorafs-v2] two-level extent tree + 130 fragmented extents persistence self-test passed on 512/4096-byte devices`

The verified tree uses 4 KiB checksummed nodes. A leaf stores up to 126 extent mappings. A level-1 root stores child logical ranges and child filesystem-block pointers, allowing the current two-level representation to describe up to 15,876 extents. Higher tree levels and online node split/merge remain future work.

## Runtime-verified inode promotion

Aurora OS Bootstrap Build **#422** (`36982594135`, head `a0465e948615a513fc9ab9da5b0a0a98a7861dfc`) completed successfully. The exact gate appears in the q35/AHCI boot and in both ATA PIO boots, including the persisted second boot:

`[aurorafs-v2] persistent inode inline-to-tree promotion at fifth extent self-test passed on 512/4096-byte devices`

The transition is therefore **runtime-verified in the synthetic QEMU environment** for devices exposing 512-byte and 4096-byte logical blocks. This is not physical-hardware 4Kn certification.

AuroraFS v2 inodes remain **256 bytes** and retain four inline extent slots. No on-disk inode size or existing field ordering changed for this milestone.

A regular file begins with `extent_tree_root == 0`. The first four appended extents are persisted directly in the inode. When a fifth extent is appended, AuroraFS v2:

1. reads the inode from the formatter-defined inode table;
2. validates the inline extent sequence and new extent;
3. builds a tree containing the four existing mappings plus the new fifth mapping;
4. persists and flushes the new tree through the common block-device and allocator layers;
5. publishes the tree root in `extent_tree_root`;
6. records total extent count five and clears the no-longer-authoritative inline slots;
7. advances inode generation, size and allocated-byte metadata;
8. persists and flushes the updated inode block.

Lookup is transparent: tree-less inodes resolve their inline entries; tree-backed inodes delegate to the verified extent-tree lookup path.

## Runtime test shape

The gate formats a fresh v2 volume, creates a regular-file inode, allocates five deliberately separated one-block data ranges, persists four inline extents and adds the fifth to force promotion. It then reopens the allocator and resolves every logical block back to its expected physical block on both synthetic 512-byte and 4096-byte devices.

## Safety and current limits

Tree data is written before the inode publishes its new root, so the inode never references an unwritten tree. The transition is not yet fully crash-transactional: if inode publication fails after tree allocation, tree blocks can be leaked. That failure mode favors leakage over metadata corruption and will be addressed by the planned copy-on-write/transaction and recovery model.

At the #422 milestone, appending another extent to an inode that has already been promoted is not yet supported. Tree-block reclamation, online split/merge, truncate/remove integration and transactional rollback are also pending.

## Active next gate — continued tree-backed append

The next implementation must allow a promoted file to continue growing. The preferred rule is copy-on-write publication: construct a replacement tree that includes the new mapping, flush it, switch the inode to the new root, then reclaim the old tree only after the inode update is durable. The first sub-gate can cover a promoted single-leaf tree; leaf-to-level-1 growth follows before general file mutation APIs are considered ready.
