# AuroraFS v2 — Level-3 extent-tree contract

Status: **level-3 lookup/growth, continued append and full-last-leaf structural COW growth are runtime-verified; persistent full-last-leaf inode publication is implemented and awaiting CI verification**.

This document extends the existing AuroraFS v2 `AUREXT2` extent-tree contract. The on-disk behavior is explicit and remains compatible with the existing 4 KiB node and 256-byte inode layouts.

## Capacity and level contract

Each 4 KiB `AUREXT2` node contains a 64-byte header and 126 32-byte entries.

- level 0 leaf: up to 126 extent mappings;
- level 1: up to 15,876 mappings;
- level 2: up to 2,000,376 mappings;
- level 3: up to 252,047,376 mappings.

The current supported maximum root level is explicitly bounded to **3**. Higher levels remain unsupported until a later documented milestone deliberately raises that limit.

No inode layout, node size, magic, checksum, entry size or node version changes are introduced by level 3. `level == 0` is a leaf; every internal node must point to children whose level is exactly one lower. Parent/child logical ranges must be monotonic, non-empty and bounded.

## Runtime-verified level-3 lookup

Workflow **`36999077531`** verified the bounded common resolver across levels 0/1/2/3 on synthetic 512-byte and 4096-byte logical-block devices.

Exact gate:

`[aurorafs-v2] bounded unified level-3 tree + inode lookup self-test passed on 512/4096-byte devices`

## Runtime-verified level-2 → level-3 growth

A full level-2 root contains 126 full level-1 children and covers 2,000,376 single-block mappings. `aurora_fs_v2_extent_tree_grow_level2_full_root_cow()` creates mapping 2,000,377 through a fresh leaf, level-1, level-2 and level-3 root while retaining the old full level-2 tree as the first child.

Main workflow **`37000379420`** and AHCI workflow **`37000379412`** verified:

`[aurorafs-v2] full level-2 root COW growth to level-3 at 2000377th extent self-test passed on 512/4096-byte devices`

Persistent inode publication through `aurora_fs_v2_inode_append_level3_grow_cow_commit()` was verified by main workflow **`37001092435`** and AHCI workflow **`37001092510`**:

`[aurorafs-v2] persistent full level-2 to level-3 growth + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

## Runtime-verified continued append under existing level-3

`aurora_fs_v2_extent_tree_append_level3_cow()` handles the next mapping while the final leaf still has spare capacity. It replaces only the final leaf/level-1/level-2 path and clones the level-3 root, flushing children before parents.

Main workflow **`37001773924`** and AHCI workflow **`37001774112`** verified:

`[aurorafs-v2] existing level-3 root COW append through final leaf/level-1/level-2/root replacement self-test passed on 512/4096-byte devices`

The persistent composed path `aurora_fs_v2_inode_append_level3_cow_commit()` was runtime-verified by Aurora OS Bootstrap Build **`37004408218`**, head `cacdbb7d42bc580e22d1a333e3be079c1cd53584`:

`[aurorafs-v2] persistent existing level-3 COW append + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

That milestone also exposed and fixed excessive kernel-stack use in its synthetic self-test. The production COW algorithm and on-disk format were unchanged; large 4 KiB test buffers were moved to static scratch storage. The same green run proves subsequent AuroraFS v1 persistence remained healthy: first ATA boot created the persistent file and the second boot reopened it.

## Runtime-verified full-last-leaf growth under existing level-3

`aurora_fs_v2_extent_tree_append_level3_full_leaf_cow()` handles the first append where the final leaf below a published level-3 root is already full. The final level-2 child is validated, then the already-verified `aurora_fs_v2_extent_tree_append_level2_full_leaf_cow()` builder creates a fresh leaf and replacement lower hierarchy. The level-3 root is cloned only after the replacement level-2 subtree is durable.

The synthetic test starts with a level-3 tree whose final branch contains a full 126-entry leaf after a 2,000,376-mapping prefix. It appends the next mapping, reopens the allocator, verifies the new and previous final mappings through the unified resolver, and proves the old level-3 root remains byte-identical.

Aurora OS Bootstrap Build **`37005018110`**, head `073302a537cbd31a2346f9b4ba20381091c085b8`, completed successfully. The exact gate appears on q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] level-3 full-last-leaf COW append with new child leaf self-test passed on 512/4096-byte devices`

The same run also reaches `[aurorafs] persistent file created` on first ATA boot and `[aurorafs] persistent file reopened` on second ATA boot, confirming the self-test does not disturb the operational v1 path.

## Persistent full-last-leaf publication — implemented / CI pending

`aurora_fs_v2_inode_append_level3_full_leaf_cow_commit()` is now implemented. It composes the verified structural builder with `aurora_fs_v2_inode_publish_extent_root_cow()` so publication remains child-first and inode-last.

The dedicated self-test:

1. seeds a persistent inode pointing at the published level-3 full-leaf hierarchy;
2. snapshots the old root;
3. builds and flushes the replacement leaf/level-1/level-2/level-3 hierarchy;
4. publishes the replacement root through the inode;
5. reopens allocator and inode state;
6. verifies root change, extent count, size, allocated bytes and generation;
7. verifies both the new mapping and the historical final mapping;
8. verifies the old published root is still byte-identical.

All large 4 KiB synthetic buffers are static scratch storage, not kernel-stack allocations.

Expected boot gate:

`[aurorafs-v2] persistent level-3 full-last-leaf COW append + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

Until a green CI run contains that exact line and the normal AuroraFS v1 persistence path still creates/reopens successfully, this milestone remains **implemented but not runtime-verified**.

## Failure and recovery behavior

The policy remains child-first and publication-last. A failure before inode publication leaves the old root authoritative and may leak newly allocated metadata. A successful inode publication makes the replacement hierarchy authoritative. Durable reclamation, transaction replay and crash recovery remain future work.

## Next structural gate

After persistent full-last-leaf publication is green, the next level-3 mutation gate is a **full final level-1 child** below the published level-3 root. The operation must create a new level-1 sibling within the final level-2 node, then clone/flush the replacement level-2 and level-3 parents before inode publication.

Synthetic 4096-byte logical-block verification is not physical 4Kn hardware certification.
