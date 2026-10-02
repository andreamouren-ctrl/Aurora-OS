# AuroraFS v2 — Persistent inode extent promotion and tree-backed growth

Status: **inline-to-tree promotion, sixth-extent COW append, 126→127 growth, persistent extent 127 publication, continued level-1 append through extent 128, and full-last-leaf growth/publication through extent 253 are runtime-verified; level-1-full-root promotion to a level-2 root at extent 15,877 is implemented and awaiting runtime promotion**.

This document is part of the AuroraFS v2 on-disk contract. Repository implementation and runtime verification are tracked separately.

## Verified extent-tree foundation

Aurora OS Bootstrap Build **#414** (`36981973396`, head `115936948dd0e9426fca2f3323e497bd7e125642`) runtime-verifies the two-level extent-tree foundation:

`[aurorafs-v2] two-level extent tree + 130 fragmented extents persistence self-test passed on 512/4096-byte devices`

Nodes are 4 KiB and checksummed. A leaf stores up to 126 extent mappings. A level-1 root stores up to 126 child ranges, for 15,876 extents before a higher level is required.

## Verified inline-to-tree inode promotion

Aurora OS Bootstrap Build **#422** (`36982594135`, head `a0465e948615a513fc9ab9da5b0a0a98a7861dfc`) is green and contains:

`[aurorafs-v2] persistent inode inline-to-tree promotion at fifth extent self-test passed on 512/4096-byte devices`

AuroraFS v2 inodes remain 256 bytes with four inline extent slots. The fifth mapping builds and flushes an external extent tree, publishes its root in the inode, clears the no-longer-authoritative inline mappings, advances generation/size/allocation metadata and flushes the inode.

## Verified post-promotion COW append

Aurora OS Bootstrap Build **#429** (`36983175716`, head `90d41b00caaed662cbc64b91d3743a05448f775c`) contains:

`[aurorafs-v2] tree-backed inode copy-on-write sixth-extent append self-test passed on 512/4096-byte devices`

## Verified full-leaf 126→127 COW growth

Aurora OS Bootstrap Build **#432** (`36983609681`, head `250cf8499fca699d26cb73b0ace274ccf0220738`) contains:

`[aurorafs-v2] full-leaf COW 126-to-127 extent growth into level-1 root self-test passed on 512/4096-byte devices`

The transition writes fresh replacement leaves plus a fresh level-1 root and never modifies the published full leaf.

## Verified persistent inode publication at extent 127

Aurora OS Bootstrap Build **#437** (`36984123103`, head `1a54f33eb0c033b5eec978918a305b942691b968`) contains:

`[aurorafs-v2] persistent inode COW publication of 127th extent through level-1 root self-test passed on 512/4096-byte devices`

`aurora_fs_v2_inode_extent_append_tree_grow_cow()` validates the persisted inode and new extent, computes overflow-sensitive size/allocation values before allocating metadata, builds the replacement hierarchy, flushes it, and then publishes the new root.

## Verified continued append to an existing level-1 root

Aurora OS Bootstrap Build **#442** (`36984773089`, head `ceb7b7d68e241e00d2198e28cee9fcc79aba8ae5`) is green. Both exact gates appear in q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] existing level-1 root COW append with last-leaf replacement self-test passed on 512/4096-byte devices`

`[aurorafs-v2] persistent inode level-1 COW publication of 128th extent self-test passed on 512/4096-byte devices`

For a level-1 root whose final leaf still has capacity, AuroraFS v2 clones the final leaf, appends the mapping, flushes it, clones the root with the new child pointer/span, flushes it, and only then publishes the new root through the inode. Old published metadata remains untouched.

## Verified full-last-leaf level-1 growth: 252→253

The path is implemented by `aurora_fs_v2_extent_tree_append_level1_full_leaf_cow()` and `aurora_fs_v2_inode_extent_append_level1_full_leaf_cow()`.

Aurora OS Bootstrap Build **#446** (`36986548697`, head `7b88e2cb7e8be0bcafb1d0768db2c588a585de6a`) completed successfully. The exact gates appear in the runtime serial path:

`[aurorafs-v2] level-1 full-last-leaf COW append with new child leaf self-test passed on 512/4096-byte devices`

`[aurorafs-v2] persistent inode level-1 COW publication of 253rd extent self-test passed on 512/4096-byte devices`

The initial geometry contains two full 126-entry leaves, or 252 mappings. The 253rd extent is written into a newly allocated leaf; a fresh level-1 root gains a third child range; the inode publishes that fresh root only after the new hierarchy is durable. The old root and old full leaf remain unchanged. The tests run on synthetic 512-byte and 4096-byte logical-block devices and reopen the allocator before validating the persistent inode mappings.

The 252→253 path is therefore **runtime-verified in the synthetic QEMU environment**. This is not physical-hardware 4Kn certification.

## Implemented level-1-full-root → level-2 growth: 15,876→15,877

The next structural boundary is now implemented in `kernel/src/fs/aurora_fs_v2_level2_growth.c`.

A full level-1 root contains 126 child leaves, each with 126 mappings, for **15,876 extents**. `aurora_fs_v2_extent_tree_grow_level1_full_root_cow()` handles extent 15,877 with a COW-only transition:

1. validates the CRC-protected full level-1 root and its full final leaf;
2. validates logical monotonicity, physical geometry and 64-bit overflow bounds;
3. creates and flushes a fresh one-entry leaf for the new extent;
4. creates and flushes a fresh one-child level-1 subtree pointing to that leaf;
5. creates and flushes a fresh **level-2 root** with two child ranges: the already-published full level-1 tree and the new level-1 subtree;
6. leaves the old full root and all 126 old leaves untouched;
7. `aurora_fs_v2_inode_extent_append_level2_grow_cow()` publishes the new level-2 root only after the replacement hierarchy is durable, then advances inode extent count, size, allocation and generation.

The level-2 node uses the existing 4 KiB / 64-byte-header / 32-byte-entry on-disk node format; the `level` field becomes `2`. No silent inode format change is introduced.

A dedicated staged resolver, `aurora_fs_v2_extent_tree_lookup_level2()`, can traverse `level 2 → level 1 → leaf`. `aurora_fs_v2_inode_extent_lookup_level2()` provides the corresponding inode lookup path. The older common `aurora_fs_v2_extent_tree_lookup()` still handles only leaf and level-1 roots; unifying the common resolver is deliberately deferred until this level-2 gate is runtime-verified.

The sparse self-test seeds 126 full leaves and a full level-1 root without allocating a giant backing disk in RAM. It validates representative mappings across every old leaf, the new 15,877th mapping, reopen behavior, and that the previously published root/final leaf were not modified. Both synthetic 512-byte and 4096-byte logical-block devices are exercised.

Expected runtime gates:

`[aurorafs-v2] full level-1 root COW growth to level-2 at 15877th extent self-test passed on 512/4096-byte devices`

`[aurorafs-v2] persistent inode publication of 15877th extent through level-2 root self-test passed on 512/4096-byte devices`

Until a green CI run contains both exact serial lines, level-2 growth remains **implemented but not runtime-verified**.

## Safety model

The current COW policy favors recoverability over immediate reclamation. Replacement nodes are written and flushed before inode publication. A crash before publication leaves the old tree authoritative and can leak replacement blocks; a crash after publication leaves the new tree authoritative while old nodes can remain allocated. Durable reclamation remains deferred to the planned transaction/recovery layer.

## Next mutation gate

After level-2 runtime verification, the next architectural step is to unify the normal extent resolver so `aurora_fs_v2_extent_tree_lookup()` transparently handles levels 0, 1 and 2. After that, continued COW append can mutate the final level-1 subtree under an existing level-2 root without exposing a separate lookup API.

All current v2 runtime gates use QEMU synthetic block devices. 4096-byte logical-block support is runtime-verified synthetically, not on physical 4Kn hardware.
