# AuroraFS v2 — Unified extent lookup and level-2 mutation

Status: **level-2 growth, unified lookup and continued COW append beneath an existing level-2 root are runtime-verified; durable inode root publication is implemented and awaiting runtime verification**.

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

## Runtime-verified COW append beneath an existing level-2 root

Aurora OS Bootstrap Build **#459** (`36990022065`, head `da7494f46b30957bb6956800f04f1e1ea78fd2c5`) completed successfully. The exact gate appears in q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] existing level-2 root COW append through leaf/level-1/root replacement self-test passed on 512/4096-byte devices`

The tree operation `aurora_fs_v2_extent_tree_append_level2_cow()` handles the first continued-mutation case after level-2 publication: the final level-1 child and its final leaf still have spare capacity.

The operation validates the published hierarchy and new extent, then performs copy-on-write bottom-up:

1. clone the final leaf, append the mapping, write the fresh leaf and flush it;
2. clone the final level-1 node, replace its final child pointer/span, write the fresh level-1 node and flush it;
3. clone the level-2 root, replace its final child pointer/span, write the fresh level-2 root and flush it;
4. return the new root while leaving the old level-2 root, old level-1 child and old leaf untouched.

The dedicated sparse self-test starts from a one-extent three-level hierarchy, appends a second extent, verifies both mappings exclusively through the unified resolver, re-reads all old nodes to prove they were not mutated, reopens the allocator and verifies the appended mapping again. It runs on synthetic 512-byte and 4096-byte logical-block devices.

## Implemented durable inode root publication

The new primitive `aurora_fs_v2_inode_publish_extent_root_cow()` separates tree durability from inode publication. It requires the caller to provide the expected old root and the already-durable replacement root. Before changing the inode it validates:

- the inode still points at the expected old root;
- the replacement root is a currently allocated AuroraFS v2 block;
- the appended extent is in range;
- inode size/allocation/count arithmetic cannot overflow.

Only then does it update `extent_tree_root`, `extent_count`, `allocated_bytes`, `size` and `generation`, write the containing inode-table block and flush the device. A stale caller therefore cannot silently publish over a different inode root.

The dedicated boot self-test persists an inode, publishes a replacement root, reopens the allocator and inode table and verifies that the new root and all counters survived on synthetic 512-byte and 4096-byte logical-block devices.

Expected boot gate:

`[aurorafs-v2] durable inode COW root publication + reopen self-test passed on 512/4096-byte devices`

Until a green CI run contains that exact line, this publication primitive remains **implemented but not runtime-verified**.

## Compatibility and safety

No on-disk structure changes are introduced by lookup unification, level-2 append or the publication primitive. Existing v2 nodes remain 4 KiB, use the `AUREXT2` format, and retain the 64-byte header / 32-byte entry layout; inodes remain 256 bytes.

The current COW policy deliberately favors recoverability over immediate reclamation. Descendant replacement nodes are durable before their parents, and the replacement root is durable before inode publication. A crash can therefore leak newly allocated metadata, but should not require in-place mutation of the previously published hierarchy. Durable reclamation remains deferred to the transaction/recovery layer.

All runtime verification in this document uses synthetic QEMU block devices. The 4096-byte logical-block path is not physical 4Kn hardware certification.

## Next gate

After durable inode root publication is runtime-verified, compose it directly with `aurora_fs_v2_extent_tree_append_level2_cow()` for an end-to-end existing-level-2 inode append. Then cover the harder case where the final leaf is full but the final level-1 node still has child capacity, followed later by a full level-1 child and eventually a full level-2 root.
