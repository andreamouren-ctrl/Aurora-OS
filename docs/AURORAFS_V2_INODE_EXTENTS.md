# AuroraFS v2 — Persistent inode extent promotion and tree-backed growth

Status: **inline-to-tree promotion, sixth-extent COW append, tree-layer 126→127 growth and persistent inode publication of extent 127 are runtime-verified; continued level-1 COW append through extent 128 is implemented and awaiting runtime promotion**.

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

Aurora OS Bootstrap Build **#437** (`36984123103`, head `1a54f33eb0c033b5eec978918a305b942691b968`) completed successfully. The exact gate appears in q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] persistent inode COW publication of 127th extent through level-1 root self-test passed on 512/4096-byte devices`

Persistent inode publication of the 127th mapping is therefore **runtime-verified in the synthetic QEMU environment** for devices exposing 512-byte and 4096-byte logical blocks. This is not physical-hardware 4Kn certification.

`aurora_fs_v2_inode_extent_append_tree_grow_cow()` validates the persisted inode and new extent, computes all overflow-sensitive size/allocation values before allocating replacement metadata, selects leaf-clone COW below 126 extents or full-leaf expansion at exactly 126, waits until the replacement hierarchy is durable, then publishes the new root and advances inode count, size, allocation and generation.

## Implemented continued append to an existing level-1 root

The first level-1 mutation path is now implemented through `aurora_fs_v2_extent_tree_append_level1_cow()` and `aurora_fs_v2_inode_extent_append_level1_cow()`.

For a published level-1 root whose final child leaf still has spare capacity, the tree operation:

1. CRC-validates the level-1 root and last child leaf;
2. validates that the new extent is monotonic and inside filesystem geometry;
3. copies the final leaf, appends the new mapping, advances leaf generation, writes it to a fresh block and flushes it;
4. copies the root, replaces only the final child pointer/span, extends the root logical end and advances root generation;
5. writes the replacement root to a fresh block and flushes it;
6. returns the new root while leaving the old root and old child leaf untouched.

The persistent inode operation pre-validates size/allocation overflow, invokes the tree COW path, then publishes the fresh root and advances extent count, logical size, allocated bytes and inode generation.

The sparse tree test starts with two children: a full 126-entry first leaf and a second leaf containing logical extent 126. It appends logical extent 127, which is the **128th extent**, verifies all 128 mappings through normal lookup, and re-reads the old root and old last leaf to prove they were not modified.

The sparse inode test starts from a persistent 127-extent inode pointing at that level-1 root, appends extent 128, publishes a fresh root, reopens the allocator and resolves all 128 logical mappings through the public inode resolver. Both tests run on synthetic 512-byte and 4096-byte logical-block devices.

Expected runtime gates:

`[aurorafs-v2] existing level-1 root COW append with last-leaf replacement self-test passed on 512/4096-byte devices`

`[aurorafs-v2] persistent inode level-1 COW publication of 128th extent self-test passed on 512/4096-byte devices`

Until a green CI run contains both exact serial lines, continued append through extent 128 remains **implemented but not runtime-verified**.

## Safety model

The current COW policy favors recoverability over immediate reclamation. Replacement nodes are written and flushed before inode publication. A crash before publication leaves the old tree authoritative and can leak replacement blocks; a crash after publication leaves the new tree authoritative while old nodes can remain allocated. Durable reclamation is intentionally deferred to the planned transaction/recovery layer.

## Next mutation gate

After runtime verification of the 128th-extent path, the next case is a level-1 root whose final child leaf is full while the root still has child capacity. AuroraFS v2 must allocate a fresh leaf for the new extent, COW the root with an additional child-range entry, flush the hierarchy and publish that new root through the inode. Root-full growth to level 2 remains a later milestone.

All current v2 runtime gates use QEMU synthetic block devices. 4096-byte logical-block support is runtime-verified synthetically, not on physical 4Kn hardware.
