# AuroraFS v2 — Unified extent lookup and level-2 mutation

Status: **level-2 growth, unified lookup, continued COW append and durable inode root publication are runtime-verified; composed level-2 append + inode publication and full-last-leaf growth under level-2 are implemented and awaiting runtime verification**.

This document is part of the AuroraFS v2 implementation contract. It distinguishes repository implementation from runtime verification.

## Runtime-verified level-2 growth

Aurora OS Bootstrap Build **#452** (`36988686741`, head `6a642d362a1df3491bab75c96de9d2bdb5c9010c`) completed successfully.

Its q35/AHCI and ATA serial logs contain the exact gates:

`[aurorafs-v2] full level-1 root COW growth to level-2 at 15877th extent self-test passed on 512/4096-byte devices`

`[aurorafs-v2] persistent inode publication of 15877th extent through level-2 root self-test passed on 512/4096-byte devices`

## Runtime-verified unified lookup front door

Aurora OS Bootstrap Build **#455** (`36989469332`, head `f213629cbd508c2999bc8e5575a81df2ae2dc70b`) completed successfully. The exact gate appears in q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] unified leaf/level-1/level-2 tree and inode lookup self-test passed on 512/4096-byte devices`

The public APIs `aurora_fs_v2_extent_tree_lookup_unified()` and `aurora_fs_v2_inode_extent_lookup_unified()` provide one lookup surface for AuroraFS v2 tree depths currently supported by the implementation.

## Runtime-verified COW append beneath an existing level-2 root

Aurora OS Bootstrap Build **#459** (`36990022065`, head `da7494f46b30957bb6956800f04f1e1ea78fd2c5`) completed successfully. The exact gate appears in q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] existing level-2 root COW append through leaf/level-1/root replacement self-test passed on 512/4096-byte devices`

`aurora_fs_v2_extent_tree_append_level2_cow()` performs the replacement bottom-up: fresh leaf, fresh level-1 node, fresh level-2 root, flushing each child before its parent while leaving the published hierarchy untouched.

## Runtime-verified durable inode root publication

Aurora OS Bootstrap Build **#462** (`36992375163`, head `fc59ac36163f684c1bdfc01406c982cd3a404d2f`) completed successfully. The exact gate appears in q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] durable inode COW root publication + reopen self-test passed on 512/4096-byte devices`

`aurora_fs_v2_inode_publish_extent_root_cow()` validates the expected old root, verifies that the replacement root is allocated, checks extent and arithmetic bounds, then updates `extent_tree_root`, `extent_count`, `allocated_bytes`, `size` and `generation` and flushes the inode-table update.

## Implemented end-to-end level-2 append + inode publication

`aurora_fs_v2_inode_append_level2_cow_commit()` composes the two verified primitives: it first builds and flushes the replacement level-2 hierarchy, then publishes the replacement root through the inode.

The self-test constructs a persistent `inode -> level-2 -> level-1 -> leaf -> extent` hierarchy, performs the append, reopens allocator and inode table, then resolves the new mapping through the unified inode lookup.

Expected boot gate:

`[aurorafs-v2] end-to-end level-2 COW append + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

Until a green CI run contains that exact line, this composed path remains **implemented but not runtime-verified**.

## Implemented full-last-leaf COW append under level-2

`aurora_fs_v2_extent_tree_append_level2_full_leaf_cow()` handles the next structural case: the final leaf is full at 126 extent mappings, while its level-1 parent still has room for another child.

The operation:

1. validates the existing level-2 root, final level-1 node and full final leaf;
2. creates a fresh one-entry leaf containing the appended extent and flushes it;
3. clones the final level-1 node, appends a new child entry pointing at the fresh leaf and flushes it;
4. clones the level-2 root, replaces the final level-1 child pointer/span and flushes it;
5. returns the replacement level-2 root without modifying any previously published root, level-1 node or leaf.

The sparse self-test starts with a level-2 hierarchy whose only leaf contains all 126 entries, appends logical block 126, verifies both the original and new mappings through the unified resolver, proves the old hierarchy remains unchanged, reopens the allocator and resolves the new mapping again. It runs on synthetic 512-byte and 4096-byte logical-block devices.

Expected boot gate:

`[aurorafs-v2] level-2 full-last-leaf COW append with new child leaf self-test passed on 512/4096-byte devices`

This path is currently **implemented but not runtime-verified**.

## Compatibility and safety

No on-disk format change is introduced. Extent-tree nodes remain 4 KiB with the existing `AUREXT2` layout and inodes remain 256 bytes.

The COW policy deliberately favors recoverability over immediate reclamation. Failure before inode publication may leak replacement metadata, but the old hierarchy remains authoritative. Durable reclamation and transaction replay remain future work.

All runtime verification described here uses synthetic QEMU block devices. The 4096-byte logical-block path is not physical 4Kn hardware certification.

## Next gate

After the tree-level full-last-leaf path is runtime-verified, compose it with durable inode publication. After that, cover the case where the final level-1 child itself is full but the level-2 root still has capacity, requiring a fresh leaf + fresh level-1 child and a cloned level-2 root with one additional child.
