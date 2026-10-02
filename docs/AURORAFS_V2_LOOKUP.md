# AuroraFS v2 — Unified extent lookup and level-2 mutation

Status: **level-2 growth and the unified leaf/level-1/level-2 lookup front door are runtime-verified; continued COW append beneath an existing level-2 root is implemented and awaiting runtime verification**.

This document is part of the AuroraFS v2 implementation contract. It distinguishes repository implementation from runtime verification.

## Runtime-verified level-2 growth

Aurora OS Bootstrap Build **#452** (`36988686741`, head `6a642d362a1df3491bab75c96de9d2bdb5c9010c`) completed successfully.

Its q35/AHCI and ATA serial logs contain the exact gates:

`[aurorafs-v2] full level-1 root COW growth to level-2 at 15877th extent self-test passed on 512/4096-byte devices`

`[aurorafs-v2] persistent inode publication of 15877th extent through level-2 root self-test passed on 512/4096-byte devices`

The verified transition starts from a full level-1 root containing 126 full leaves, or 15,876 single-block extent mappings. The 15,877th extent is written through a fresh leaf and fresh level-1 branch, followed by a fresh level-2 root. The previously published level-1 tree remains untouched until the inode publishes the new root.

## Runtime-verified unified lookup front door

Aurora OS Bootstrap Build **#455** (`36989469332`, head `f213629cbd508c2999bc8e5575a81df2ae2dc70b`) completed successfully. The exact gate appears in q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] unified leaf/level-1/level-2 tree and inode lookup self-test passed on 512/4096-byte devices`

The public APIs:

- `aurora_fs_v2_extent_tree_lookup_unified()`
- `aurora_fs_v2_inode_extent_lookup_unified()`

provide one lookup surface for AuroraFS v2 tree depths currently supported by the implementation. They delegate to the depth-aware resolver, which accepts leaf, level-1 and level-2 roots.

The dedicated self-test constructs a persistent hierarchy:

`level-2 root -> level-1 root -> leaf -> data extent`

It resolves the same mapping through the unified tree API and through an inode whose `extent_tree_root` points to the level-2 hierarchy, on synthetic 512-byte and 4096-byte logical-block devices.

## Implemented COW append beneath an existing level-2 root

The new tree operation `aurora_fs_v2_extent_tree_append_level2_cow()` handles the first continued-mutation case after level-2 publication: the final level-1 child and its final leaf still have spare capacity.

The operation validates the published hierarchy and new extent, then performs copy-on-write bottom-up:

1. clone the final leaf, append the mapping, write the fresh leaf and flush it;
2. clone the final level-1 node, replace its final child pointer/span, write the fresh level-1 node and flush it;
3. clone the level-2 root, replace its final child pointer/span, write the fresh level-2 root and flush it;
4. return the new root while leaving the old level-2 root, old level-1 child and old leaf untouched.

The dedicated sparse self-test starts from a one-extent three-level hierarchy, appends a second extent, verifies both mappings exclusively through the unified resolver, re-reads all old nodes to prove they were not mutated, reopens the allocator and verifies the appended mapping again. It runs on synthetic 512-byte and 4096-byte logical-block devices.

Expected boot gate:

`[aurorafs-v2] existing level-2 root COW append through leaf/level-1/root replacement self-test passed on 512/4096-byte devices`

Until a green CI run contains that exact line, this level-2 append path remains **implemented but not runtime-verified**.

## Compatibility and safety

No on-disk structure changes are introduced by lookup unification or the new level-2 append path. Existing v2 nodes remain 4 KiB, use the `AUREXT2` format, and retain the 64-byte header / 32-byte entry layout.

The current COW policy deliberately favors recoverability over immediate reclamation. Descendant replacement nodes are durable before their parents, and the replacement level-2 root is durable before a future inode publication step. A crash can therefore leak newly allocated metadata, but should not require in-place mutation of the previously published hierarchy. Durable reclamation remains deferred to the transaction/recovery layer.

All runtime verification in this document uses synthetic QEMU block devices. The 4096-byte logical-block path is not physical 4Kn hardware certification.

## Next gate

After the tree-level append gate is runtime-verified, add persistent inode publication for that replacement level-2 root. Then cover the harder case where the final leaf is full but the final level-1 node still has child capacity, followed later by a full level-1 child and eventually a full level-2 root.
