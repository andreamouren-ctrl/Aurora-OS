# AuroraFS v2 — Unified extent lookup

Status: **level-2 growth is runtime-verified; unified lookup front door is implemented and awaiting runtime verification**.

This document is part of the AuroraFS v2 implementation contract. It distinguishes repository implementation from runtime verification.

## Runtime-verified level-2 growth

Aurora OS Bootstrap Build **#452** (`36988686741`, head `6a642d362a1df3491bab75c96de9d2bdb5c9010c`) completed successfully.

Its q35/AHCI and ATA serial logs contain the exact gates:

`[aurorafs-v2] full level-1 root COW growth to level-2 at 15877th extent self-test passed on 512/4096-byte devices`

`[aurorafs-v2] persistent inode publication of 15877th extent through level-2 root self-test passed on 512/4096-byte devices`

The verified transition starts from a full level-1 root containing 126 full leaves, or 15,876 single-block extent mappings. The 15,877th extent is written through a fresh leaf and fresh level-1 branch, followed by a fresh level-2 root. The previously published level-1 tree remains untouched until the inode publishes the new root.

This is runtime verification on synthetic QEMU block devices exposing 512-byte and 4096-byte logical blocks. It is not physical 4Kn hardware certification.

## Unified lookup front door

The public APIs:

- `aurora_fs_v2_extent_tree_lookup_unified()`
- `aurora_fs_v2_inode_extent_lookup_unified()`

provide one lookup surface for AuroraFS v2 tree depths currently supported by the implementation. They delegate to the depth-aware level-2 resolver, which accepts leaf, level-1 and level-2 roots.

The dedicated self-test constructs a persistent three-stage hierarchy:

`level-2 root -> level-1 root -> leaf -> data extent`

It then resolves the same mapping through the unified tree API and through an inode whose `extent_tree_root` points to the level-2 hierarchy. The test runs independently on synthetic 512-byte and 4096-byte logical-block devices.

Expected boot gate:

`[aurorafs-v2] unified leaf/level-1/level-2 tree and inode lookup self-test passed on 512/4096-byte devices`

Until a green CI run contains that exact line, the unified front door remains **implemented but not runtime-verified**.

## Compatibility

The older leaf/level-1 resolver and the staged level-2 resolver remain available during migration. No on-disk structure is changed by this lookup unification step. Existing v2 nodes remain 4 KiB, use the `AUREXT2` format, and retain the 64-byte header / 32-byte entry layout.

## Next gate

After unified lookup is runtime-verified, migrate internal callers to the unified API and implement continued copy-on-write append beneath an already-published level-2 root. Descendants must become durable before a replacement level-2 root is published through the inode. Durable reclamation of superseded COW metadata remains deferred to the future transaction/recovery layer.
