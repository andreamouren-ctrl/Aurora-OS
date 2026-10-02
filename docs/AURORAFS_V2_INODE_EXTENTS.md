# AuroraFS v2 — Persistent inode extent promotion and tree-backed growth

Status: **inline-to-tree promotion, sixth-extent COW append, tree-layer 126→127 growth and persistent inode publication of extent 127 are runtime-verified; continued append to an existing level-1 tree is the active milestone**.

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

The sixth-extent path is runtime-verified in the synthetic QEMU environment for devices exposing 512-byte and 4096-byte logical blocks. This is not physical-hardware 4Kn certification.

## Verified full-leaf 126→127 COW growth

Aurora OS Bootstrap Build **#432** (`36983609681`, head `250cf8499fca699d26cb73b0ace274ccf0220738`) completed successfully. Its serial log contains:

`[aurorafs-v2] full-leaf COW 126-to-127 extent growth into level-1 root self-test passed on 512/4096-byte devices`

The tree-layer transition writes two fresh leaves plus a fresh level-1 root and never modifies the published full leaf.

## Verified persistent inode publication at extent 127

Aurora OS Bootstrap Build **#437** (`36984123103`, head `1a54f33eb0c033b5eec978918a305b942691b968`) completed successfully. The exact gate appears in the q35/AHCI boot, the ATA first boot and the ATA persistence boot:

`[aurorafs-v2] persistent inode COW publication of 127th extent through level-1 root self-test passed on 512/4096-byte devices`

Persistent inode publication of the 127th mapping is therefore **runtime-verified in the synthetic QEMU environment** for devices exposing 512-byte and 4096-byte logical blocks. This is not physical-hardware 4Kn certification.

`aurora_fs_v2_inode_extent_append_tree_grow_cow()` validates the persisted inode and new extent, computes all overflow-sensitive size/allocation values before allocating replacement metadata, selects leaf-clone COW below 126 extents or full-leaf expansion at exactly 126, waits until the replacement hierarchy is durable, then publishes the new root and advances inode count, size, allocation and generation.

The sparse persistence gate begins with an inode pointing at a full 126-entry leaf, appends extent 127, verifies a new level-1 root was published, reopens the allocator and resolves all 127 mappings through the public inode lookup path.

## Safety model

The current COW policy favors recoverability over immediate reclamation. Replacement nodes are written and flushed before inode publication. A crash before publication leaves the old tree authoritative and can leak replacement blocks; a crash after publication leaves the new tree authoritative while old nodes can remain allocated. Durable reclamation is intentionally deferred to the planned transaction/recovery layer.

## Active milestone — append to an existing level-1 root

The next mutation path starts from the already-published two-child level-1 tree produced at extent 127. For extent 128 and later, while the last child leaf still has spare capacity, AuroraFS v2 will:

1. CRC-validate the level-1 root and its last child leaf;
2. copy that last leaf into a fresh block and append the new extent;
3. flush the replacement leaf;
4. copy the root into a fresh block, replace the last child pointer/range and extend the root logical range;
5. flush the replacement root;
6. publish the replacement root through the inode;
7. leave the old root and old child untouched until transactional reclamation exists.

A later sub-gate will handle the case where the last child leaf is full but the level-1 root still has child capacity by adding a new child leaf. Root-full growth to level 2 remains a subsequent milestone.

All current v2 runtime gates use QEMU synthetic block devices. 4096-byte logical-block support is runtime-verified synthetically, not on physical 4Kn hardware.
