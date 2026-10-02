# AuroraFS v2 — Persistent inode extent promotion and tree-backed growth

Status: **inline-to-tree promotion runtime-verified; first post-promotion copy-on-write append implemented and awaiting runtime promotion**.

This document is part of the AuroraFS v2 on-disk contract. Repository implementation and runtime verification are tracked separately.

## Verified extent-tree foundation

Aurora OS Bootstrap Build **#414** (`36981973396`, head `115936948dd0e9426fca2f3323e497bd7e125642`) runtime-verifies the current two-level extent-tree foundation. Its serial log contains:

`[aurorafs-v2] two-level extent tree + 130 fragmented extents persistence self-test passed on 512/4096-byte devices`

Nodes are 4 KiB and checksummed. A leaf stores up to 126 extent mappings. A level-1 root stores up to 126 child ranges, so the current two-level representation can describe up to 15,876 extents. Higher levels and online split/merge remain future work.

## Verified inline-to-tree inode promotion

Aurora OS Bootstrap Build **#422** (`36982594135`, head `a0465e948615a513fc9ab9da5b0a0a98a7861dfc`) is green. The exact gate appears in the q35/AHCI boot and both ATA PIO boots:

`[aurorafs-v2] persistent inode inline-to-tree promotion at fifth extent self-test passed on 512/4096-byte devices`

The transition is therefore **runtime-verified in the synthetic QEMU environment** for devices exposing 512-byte and 4096-byte logical blocks. It is not physical-hardware 4Kn certification.

AuroraFS v2 inodes remain 256 bytes with four inline extent slots. The first four mappings remain inline. The fifth builds and flushes an external extent tree, publishes its root in the inode, clears the no-longer-authoritative inline mappings, advances generation/size/allocation metadata and flushes the inode. Lookup is transparent between inline and tree-backed states.

## Implemented post-promotion COW append

The next primitive is now implemented for an already-promoted inode whose current tree root is a **single leaf with spare capacity**.

`aurora_fs_v2_extent_tree_clone_append_leaf()` reads and CRC-validates the published leaf, validates the new logical/physical range, copies the node, appends the new mapping, advances the node generation, allocates a fresh filesystem block, writes and flushes the replacement node, and returns the new root block. The old published node is never modified in place.

`aurora_fs_v2_inode_extent_append_tree_cow()` then publishes that new root in the persistent inode and advances extent count, allocated bytes, logical size and inode generation. The first gate therefore exercises the transition from five extents to six extents after the fifth-extent promotion.

The boot self-test formats a fresh v2 volume, creates a file inode, allocates six deliberately separated data blocks, persists the first four inline, promotes on the fifth, performs the sixth append through the COW tree path, reopens the allocator and resolves all six logical mappings again on both synthetic 512-byte and 4096-byte devices.

Expected runtime gate:

`[aurorafs-v2] tree-backed inode copy-on-write sixth-extent append self-test passed on 512/4096-byte devices`

Until a green CI run contains that exact line, this sixth-extent path remains **implemented but not runtime-verified**.

## Safety model and known limits

The COW append favors recoverability over space efficiency: the new tree node is durable before the inode publishes it, while the old root is intentionally left allocated for now. A crash before inode publication leaves the old inode/tree valid and may leak the new node. A crash after publication leaves the new tree valid and may leave the old node allocated. Durable reclamation requires the later transaction/recovery layer.

This first post-promotion milestone supports only a leaf root with spare capacity. It does **not** yet handle leaf-full conversion into a level-1 root, appending to an existing level-1 tree, node split/merge, old-tree reclamation, truncate/remove integration or transactional rollback.

## Next gates

After runtime verification of the sixth-extent COW append, development proceeds to leaf-full growth: when extent 127 is added to a full 126-entry leaf, AuroraFS v2 must build two new leaves plus a new level-1 root using the same publish-after-flush rule. After that, continued append to an existing level-1 root and safe reclamation become prerequisites for general create/truncate/remove/rename operations.
