# AuroraFS v2 — Unified extent lookup and level-2 mutation

Status: **level-2 growth, unified lookup, continued COW append, durable inode publication, full-last-leaf growth/publication and full final level-1 sibling growth are runtime-verified; persistent publication of full-level1 sibling growth is implemented and awaiting runtime verification**.

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

`aurora_fs_v2_inode_append_level2_cow_commit()` composes the verified tree replacement and inode publication paths.

## Runtime-verified full-last-leaf COW append under level-2

Workflow `36994521731` (head `e65c70759264ca8c7d451e22e01e1251ec443048`) completed successfully. Its q35/AHCI, ATA first-boot and ATA persistence-boot serial logs contain:

`[aurorafs-v2] level-2 full-last-leaf COW append with new child leaf self-test passed on 512/4096-byte devices`

`aurora_fs_v2_extent_tree_append_level2_full_leaf_cow()` creates a fresh leaf, clones the final level-1 parent with a new child, then clones the level-2 root while preserving the old published hierarchy.

## Runtime-verified persistent full-last-leaf publication

Workflow `36996338607` (head `9c56e9de28e727a2e52a21a02374abf93a94b1de`) completed successfully. The exact gate appears on q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] persistent level-2 full-last-leaf COW append + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

`aurora_fs_v2_inode_append_level2_full_leaf_cow_commit()` preserves child-first/publication-last ordering: replacement leaf, replacement level-1, replacement level-2 root, then inode publication and flush.

## Runtime-verified full final level-1 sibling growth under level-2

Workflow `36997766355` (head `e19fb799d3be5a6ddc4eb8b01efcd35d6b10f4cb`) completed successfully. The exact gate appears in the q35/BIOS serial log, ATA first boot and ATA persistence boot:

`[aurorafs-v2] level-2 full level-1 sibling COW growth self-test passed on 512/4096-byte devices`

The corresponding AHCI end-to-end workflow `36997766251` also completed the AHCI smoke path successfully with the same structural gate before the storage traversal.

`aurora_fs_v2_extent_tree_append_level2_full_level1_cow()` handles a final level-1 node containing all 126 child leaves, each with 126 mappings, while the level-2 root still has spare capacity. A completely full level-1 subtree therefore represents **15,876 extent mappings**.

The operation:

1. validates the level-2 root, full final level-1 child and full final leaf;
2. creates and flushes a fresh one-entry leaf;
3. creates and flushes a fresh one-child level-1 sibling;
4. clones the level-2 root and appends the sibling as another child;
5. flushes the replacement root and leaves the old root/subtree untouched.

The self-test synthesizes the 126 historical leaf metadata blocks deterministically on read and stores only the bitmap plus a small bounded cache for newly written COW nodes. This avoids the earlier high-BSS harness while still exercising the complete 15,876-mapping structural boundary on synthetic 512-byte and 4096-byte logical-block devices.

## Implemented persistent full-level1 sibling publication

`aurora_fs_v2_inode_append_level2_full_level1_cow_commit()` composes the verified sibling-growth builder with `aurora_fs_v2_inode_publish_extent_root_cow()`.

The persistent self-test starts with an inode referencing a completely full 15,876-mapping level-1 subtree beneath a level-2 root. It appends mapping 15,877, publishes the replacement level-2 root into the inode, reopens allocator/inode state, validates `extent_tree_root`, `extent_count`, `size`, `allocated_bytes` and `generation`, then resolves both the newly appended mapping and an old mapping through the unified inode resolver.

The test keeps the same sparse/generated historical-tree model, adding only one real 4 KiB inode block plus a bounded cache for new COW metadata. It runs on synthetic 512-byte and 4096-byte logical-block devices.

Expected boot gate:

`[aurorafs-v2] persistent level-2 full-level1 sibling growth + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

Until a green CI run contains that exact line, this composed path remains **implemented but not runtime-verified**.

## Compatibility and safety

No on-disk format change is introduced. Extent-tree nodes remain 4 KiB with the existing `AUREXT2` layout and inodes remain 256 bytes.

The COW policy deliberately favors recoverability over immediate reclamation. Failure before inode publication may leak replacement metadata, but the old hierarchy remains authoritative. Durable reclamation and transaction replay remain future work.

All runtime verification described here uses synthetic QEMU block devices. The 4096-byte logical-block path is not physical 4Kn hardware certification.

## Next gate

Runtime-verify persistent publication of the full-level1 sibling-growth path. After that, define the architecture for a **completely full level-2 root** (126 full level-1 children = **2,000,376 extent mappings**). That boundary requires an explicit deeper-tree or separately versioned extent-index contract; AuroraFS v2 must not silently exceed the current on-disk node-level contract.
