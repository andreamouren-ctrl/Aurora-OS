# AuroraFS v2 — Unified extent lookup and level-2 mutation

Status: **level-2 growth, unified lookup, continued COW append, durable inode publication, full-last-leaf structural growth and persistent full-last-leaf publication are runtime-verified; full final level-1 sibling growth under level-2 is implemented and awaiting runtime verification**.

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

## Runtime-verified end-to-end level-2 append + inode publication

Aurora OS Bootstrap Build **#465** (`36994012534`, head `567c5dd27297200a711fcabcd5de64844a932386`) completed successfully. The exact gate appears in q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] end-to-end level-2 COW append + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

`aurora_fs_v2_inode_append_level2_cow_commit()` composes the verified tree replacement and inode publication paths. The self-test constructs a persistent `inode -> level-2 -> level-1 -> leaf -> extent` hierarchy, performs the append, reopens allocator and inode table, then resolves the new mapping through the unified inode lookup.

## Runtime-verified full-last-leaf COW append under level-2

Workflow `36994521731` (head `e65c70759264ca8c7d451e22e01e1251ec443048`) completed successfully. Its q35/AHCI, ATA first-boot and ATA persistence-boot serial logs contain the exact gate:

`[aurorafs-v2] level-2 full-last-leaf COW append with new child leaf self-test passed on 512/4096-byte devices`

`aurora_fs_v2_extent_tree_append_level2_full_leaf_cow()` handles the structural case where the final leaf is full at 126 extent mappings while its level-1 parent still has room for another child.

The operation:

1. validates the existing level-2 root, final level-1 node and full final leaf;
2. creates a fresh one-entry leaf containing the appended extent and flushes it;
3. clones the final level-1 node, appends a new child entry pointing at the fresh leaf and flushes it;
4. clones the level-2 root, replaces the final level-1 child pointer/span and flushes it;
5. returns the replacement level-2 root without modifying any previously published root, level-1 node or leaf.

## Runtime-verified persistent full-last-leaf publication

Workflow `36996338607` (head `9c56e9de28e727a2e52a21a02374abf93a94b1de`) completed successfully. The exact gate appears on q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] persistent level-2 full-last-leaf COW append + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

`aurora_fs_v2_inode_append_level2_full_leaf_cow_commit()` composes the verified structural full-leaf tree replacement with `aurora_fs_v2_inode_publish_extent_root_cow()`.

The ordering is child-first and publication-last:

1. allocate and flush the replacement leaf;
2. allocate and flush the replacement level-1 parent;
3. allocate and flush the replacement level-2 root;
4. verify the inode still references the expected old root;
5. publish the replacement root in the inode and flush the inode-table block.

The end-to-end self-test reopens allocator and inode state, verifies counters and generation, resolves the appended mapping through the unified inode lookup, and re-resolves an old mapping. Both synthetic 512-byte and 4096-byte logical-block devices pass.

## Implemented full final level-1 sibling growth under level-2

`aurora_fs_v2_extent_tree_append_level2_full_level1_cow()` handles the next structural boundary. The final level-1 node already contains all 126 child leaves and its last leaf also contains all 126 mappings, while the level-2 root still has spare child capacity.

A completely full level-1 subtree represents **15,876 extent mappings**. Appending the next mapping cannot modify or extend that subtree in place.

The operation therefore:

1. validates the level-2 root, its full final level-1 child, and that child's full final leaf;
2. validates the new extent geometry and monotonic logical range;
3. creates and flushes a fresh one-entry leaf;
4. creates and flushes a fresh one-child level-1 sibling pointing at that leaf;
5. clones the level-2 root, appends one new child entry for the sibling, updates generation/range, and flushes the replacement root;
6. leaves the old root, old full level-1 node, and old leaves untouched.

The sparse self-test materializes all 126 leaf metadata nodes with 126 mappings each without allocating backing buffers for data blocks, then appends mapping 15,877. It verifies lookup of both an old and the new mapping, proves the old hierarchy is unchanged, confirms the replacement root gained a second level-1 child, reopens the allocator, and resolves the appended mapping again. It runs on synthetic 512-byte and 4096-byte logical-block devices.

Expected boot gate:

`[aurorafs-v2] level-2 full level-1 sibling COW growth self-test passed on 512/4096-byte devices`

Until a green CI run contains that exact line, this path remains **implemented but not runtime-verified**.

## Compatibility and safety

No on-disk format change is introduced. Extent-tree nodes remain 4 KiB with the existing `AUREXT2` layout and inodes remain 256 bytes.

The COW policy deliberately favors recoverability over immediate reclamation. Failure before inode publication may leak replacement metadata, but the old hierarchy remains authoritative. Durable reclamation and transaction replay remain future work.

All runtime verification described here uses synthetic QEMU block devices. The 4096-byte logical-block path is not physical 4Kn hardware certification.

## Next gate

Runtime-verify the full-final-level-1 sibling growth path. Once green, compose the returned replacement level-2 root with durable inode publication and verify reopen lookup. The later structural boundary is a completely full level-2 root, which will require either a deeper tree level or a separately versioned extent-index strategy rather than silently exceeding the current node format.
