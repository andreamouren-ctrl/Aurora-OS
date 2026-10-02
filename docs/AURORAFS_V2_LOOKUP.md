# AuroraFS v2 — Unified extent lookup and level-2 mutation

Status: **level-2 growth, unified lookup, continued COW append and durable inode root publication are runtime-verified; composed level-2 append + inode publication is implemented and awaiting runtime verification**.

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

provide one lookup surface for AuroraFS v2 tree depths currently supported by the implementation.

## Runtime-verified COW append beneath an existing level-2 root

Aurora OS Bootstrap Build **#459** (`36990022065`, head `da7494f46b30957bb6956800f04f1e1ea78fd2c5`) completed successfully. The exact gate appears in q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] existing level-2 root COW append through leaf/level-1/root replacement self-test passed on 512/4096-byte devices`

`aurora_fs_v2_extent_tree_append_level2_cow()` performs the replacement bottom-up: fresh leaf, fresh level-1 node, fresh level-2 root, flushing each child before its parent while leaving the published hierarchy untouched.

## Runtime-verified durable inode root publication

Aurora OS Bootstrap Build **#462** (`36992375163`, head `fc59ac36163f684c1bdfc01406c982cd3a404d2f`) completed successfully. The exact gate appears in q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] durable inode COW root publication + reopen self-test passed on 512/4096-byte devices`

`aurora_fs_v2_inode_publish_extent_root_cow()` validates the expected old root, verifies that the replacement root is allocated, checks extent and arithmetic bounds, then updates `extent_tree_root`, `extent_count`, `allocated_bytes`, `size` and `generation` and flushes the inode-table update.

This keeps tree construction and root publication separate, so a replacement hierarchy can become durable before the inode starts referencing it.

## Implemented end-to-end level-2 append + inode publication

`aurora_fs_v2_inode_append_level2_cow_commit()` composes the two verified primitives:

1. build and flush a replacement level-2 hierarchy with `aurora_fs_v2_extent_tree_append_level2_cow()`;
2. publish that replacement root with `aurora_fs_v2_inode_publish_extent_root_cow()`;
3. leave the old published hierarchy untouched until the inode switch succeeds.

The dedicated self-test constructs a real persistent `inode -> level-2 -> level-1 -> leaf -> extent` hierarchy, performs the append, reopens the allocator and inode table, then resolves the newly appended logical block through `aurora_fs_v2_inode_extent_lookup_unified()`. It runs on synthetic 512-byte and 4096-byte logical-block devices.

Expected boot gate:

`[aurorafs-v2] end-to-end level-2 COW append + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

Until a green CI run contains that exact line, this composed path remains **implemented but not runtime-verified**.

## Compatibility and safety

No on-disk format change is introduced here. Extent-tree nodes remain 4 KiB with the existing `AUREXT2` layout and inodes remain 256 bytes.

The current COW policy deliberately favors recoverability over immediate reclamation. If failure occurs after replacement metadata allocation but before inode publication, blocks may leak but the old hierarchy remains authoritative. Reclamation and transaction replay remain future work.

All runtime verification described here uses synthetic QEMU block devices. The 4096-byte logical-block path is not physical 4Kn hardware certification.

## Next gate

After the composed path is runtime-verified, cover the harder continued-mutation case where the final leaf under an existing level-2 root is full but its level-1 parent still has child capacity. That path must create a new leaf, clone the level-1 parent with an added child, clone the level-2 root, then publish the replacement root through the inode.
