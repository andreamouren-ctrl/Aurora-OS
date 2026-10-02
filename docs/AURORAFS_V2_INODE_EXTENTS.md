# AuroraFS v2 — Persistent inode extent promotion and tree-backed growth

Status: **inline-to-tree promotion and sixth-extent copy-on-write append runtime-verified; full-leaf 126→127 tree growth implemented and awaiting runtime promotion**.

This document is part of the AuroraFS v2 on-disk contract. Repository implementation and runtime verification are tracked separately.

## Verified extent-tree foundation

Aurora OS Bootstrap Build **#414** (`36981973396`, head `115936948dd0e9426fca2f3323e497bd7e125642`) runtime-verifies the two-level extent-tree foundation:

`[aurorafs-v2] two-level extent tree + 130 fragmented extents persistence self-test passed on 512/4096-byte devices`

Nodes are 4 KiB and checksummed. A leaf stores up to 126 extent mappings. A level-1 root stores up to 126 child ranges, so the current two-level representation can describe up to 15,876 extents. Higher levels remain future work.

## Verified inline-to-tree inode promotion

Aurora OS Bootstrap Build **#422** (`36982594135`, head `a0465e948615a513fc9ab9da5b0a0a98a7861dfc`) is green and contains:

`[aurorafs-v2] persistent inode inline-to-tree promotion at fifth extent self-test passed on 512/4096-byte devices`

AuroraFS v2 inodes remain 256 bytes with four inline extent slots. The first four mappings remain inline; the fifth builds and flushes an external extent tree, publishes its root in the inode, clears the no-longer-authoritative inline mappings, advances generation/size/allocation metadata and flushes the inode.

## Verified post-promotion COW append

Aurora OS Bootstrap Build **#429** (`36983175716`, head `90d41b00caaed662cbc64b91d3743a05448f775c`) completed successfully. The exact sixth-extent gate appears in the q35/AHCI boot, the ATA first boot and the ATA persistence boot:

`[aurorafs-v2] tree-backed inode copy-on-write sixth-extent append self-test passed on 512/4096-byte devices`

The sixth-extent path is therefore **runtime-verified in the synthetic QEMU environment** for devices exposing 512-byte and 4096-byte logical blocks. This is not physical-hardware 4Kn certification.

`aurora_fs_v2_extent_tree_clone_append_leaf()` CRC-validates the published leaf, copies it into a newly allocated filesystem block, appends the new mapping, advances node generation, writes and flushes the replacement node, and leaves the old published node untouched. `aurora_fs_v2_inode_extent_append_tree_cow()` then publishes the new root through the inode and advances extent count, allocated bytes, logical size and inode generation.

The safety policy deliberately favors recoverability over immediate space reclamation. A crash before inode publication leaves the old tree valid and can leak the replacement node; a crash after publication leaves the new tree valid and can leave the old node allocated. Durable reclamation is deferred to the later transaction/recovery layer.

## Implemented full-leaf 126→127 COW growth

The next boundary is now implemented in `aurora_fs_v2_extent_tree_expand_full_leaf_cow()`. It accepts only a CRC-valid published leaf containing exactly 126 mappings and a new monotonic extent that would become mapping 127.

The operation never edits the published leaf. Instead it:

1. validates the old full leaf and the new logical/physical range;
2. allocates a fresh first leaf and writes a copy of the original 126 mappings;
3. allocates a fresh second leaf containing the 127th mapping;
4. flushes both leaves;
5. allocates a fresh level-1 root containing two child-range entries;
6. writes and flushes the new root;
7. returns the new root for later inode publication;
8. leaves the original published leaf allocated and unchanged.

The self-test uses a sparse virtual block device rather than a multi-megabyte backing array. It persists a full 126-entry leaf, performs the COW expansion, verifies that the new root is level 1 with two children, resolves all 127 logical mappings through the normal extent-tree lookup path, and finally re-reads the old root to prove it was not modified. The same test runs with synthetic 512-byte and 4096-byte logical block devices.

Expected runtime gate:

`[aurorafs-v2] full-leaf COW 126-to-127 extent growth into level-1 root self-test passed on 512/4096-byte devices`

Until a green CI run contains that exact serial line, the 126→127 transition remains **implemented but not runtime-verified**.

## Current limits

The new 126→127 primitive is currently a tree-layer transition. Publication of this newly created level-1 root through a live inode is the next integration step. Continued append to an already level-1 tree, COW replacement of its last leaf/root, node split/merge, old-tree reclamation, truncate/remove integration and transaction rollback are still pending.

All current v2 runtime gates use QEMU synthetic block devices. Support for 4096-byte logical blocks is runtime-verified synthetically, not on physical 4Kn hardware.

## Next gates

After runtime verification of the 126→127 tree transition, AuroraFS v2 will integrate that transition into the persistent inode append path. The following gate will then support continued append to an existing level-1 root: COW the last leaf when it has room, or add a new leaf when it is full, COW the root, flush the replacement hierarchy, and publish the new root through the inode. Safe old-tree reclamation follows with the transaction/recovery model.
