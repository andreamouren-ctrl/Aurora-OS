# AuroraFS v2 — Persistent inode extent promotion and tree-backed growth

Status: **inline-to-tree promotion, sixth-extent COW append and tree-layer 126→127 growth runtime-verified; persistent inode publication of the 127th extent implemented and awaiting runtime promotion**.

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

Aurora OS Bootstrap Build **#429** (`36983175716`, head `90d41b00caaed662cbc64b91d3743a05448f775c`) completed successfully. The exact sixth-extent gate appears in q35/AHCI and both ATA boots:

`[aurorafs-v2] tree-backed inode copy-on-write sixth-extent append self-test passed on 512/4096-byte devices`

The sixth-extent path is therefore runtime-verified in the synthetic QEMU environment for devices exposing 512-byte and 4096-byte logical blocks. This is not physical-hardware 4Kn certification.

`aurora_fs_v2_extent_tree_clone_append_leaf()` CRC-validates the published leaf, copies it into a newly allocated filesystem block, appends the mapping, advances node generation, writes and flushes the replacement node, and leaves the old published node untouched. The inode then publishes the new root and advances extent count, allocated bytes, logical size and generation.

## Verified full-leaf 126→127 COW growth

Aurora OS Bootstrap Build **#432** (`36983609681`, head `250cf8499fca699d26cb73b0ace274ccf0220738`) completed successfully. Its serial log contains the exact gate on q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] full-leaf COW 126-to-127 extent growth into level-1 root self-test passed on 512/4096-byte devices`

The tree-layer 126→127 transition is therefore **runtime-verified in the synthetic QEMU environment** for 512-byte and 4096-byte logical-block devices.

`aurora_fs_v2_extent_tree_expand_full_leaf_cow()` never edits the published full leaf. It writes a fresh copy of the original 126 mappings, writes a second leaf containing mapping 127, flushes both leaves, writes and flushes a fresh level-1 root with two child ranges, and returns that root. The old published leaf remains allocated and unchanged.

The sparse self-test resolves all 127 logical mappings through the normal extent-tree lookup path and re-reads the old root to prove it was not modified.

## Implemented persistent inode publication at mapping 127

The tree transition is now integrated with a persistent inode through `aurora_fs_v2_inode_extent_append_tree_grow_cow()`.

Before allocating replacement metadata, the operation validates the inode and extent and computes all logical-size, allocated-byte and integer-overflow results. It then selects the COW strategy from the persisted extent count:

- for tree-backed files with fewer than 126 extents, it clones and appends to a replacement leaf;
- for a file with exactly 126 extents, it invokes the verified full-leaf expansion into two fresh leaves plus a fresh level-1 root;
- counts above 126 are rejected by this first integration because continued mutation of an already level-1 tree is the next milestone.

Only after the replacement tree has been written and flushed does the inode publish the new root, increment extent count, update logical size and allocated bytes, advance generation and flush the inode block.

The new sparse persistence test starts with a real 256-byte inode whose `extent_tree_root` points at a persisted full 126-entry leaf. It appends logical extent 126 as the 127th mapping, verifies that the inode publishes a different root and advances generation/count/size/allocation metadata, reopens the allocator, and resolves all 127 mappings through the public inode lookup path on both synthetic 512-byte and 4096-byte devices.

Expected runtime gate:

`[aurorafs-v2] persistent inode COW publication of 127th extent through level-1 root self-test passed on 512/4096-byte devices`

Until a green CI run contains that exact serial line, inode publication of the 127th extent remains **implemented but not runtime-verified**.

## Safety model and current limits

The current COW policy favors recoverability over immediate reclamation. Replacement nodes become durable before the inode publishes them. A crash before publication leaves the old tree authoritative and can leak newly allocated nodes; a crash after publication leaves the new tree authoritative while older tree nodes can remain allocated. Durable reclamation is intentionally deferred to the planned transaction/recovery layer.

The current persistent append integration stops at 127 extents. Continued append to an already level-1 root, COW replacement of its last child leaf/root, addition of further child leaves, root-full handling, old-tree reclamation, truncate/remove integration and transactional rollback are pending.

All current v2 runtime gates use QEMU synthetic block devices. 4096-byte logical-block support is runtime-verified synthetically, not on physical 4Kn hardware.

## Next gate

After runtime verification of persistent inode publication at extent 127, AuroraFS v2 will support continued append to an existing level-1 root. The first case will COW the last child leaf when it has spare capacity, COW and flush the parent root, then publish the replacement root through the inode. The following case will add a new child leaf when the last leaf is full and the root still has child capacity. Safe reclamation follows with the transaction/recovery model.
