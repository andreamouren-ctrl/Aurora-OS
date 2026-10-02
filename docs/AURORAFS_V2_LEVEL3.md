# AuroraFS v2 — Level-3 extent-tree contract

Status: **level-3 lookup/growth, continued append, full-last-leaf structural growth and persistent full-last-leaf inode publication are runtime-verified**.

This document extends the existing AuroraFS v2 `AUREXT2` extent-tree contract. The on-disk behavior remains compatible with the existing 4 KiB node and 256-byte inode layouts.

## Capacity and level contract

Each 4 KiB `AUREXT2` node contains a 64-byte header and 126 32-byte entries.

- level 0 leaf: up to 126 extent mappings;
- level 1: up to 15,876 mappings;
- level 2: up to 2,000,376 mappings;
- level 3: up to 252,047,376 mappings.

The current supported maximum root level is explicitly bounded to **3**. Higher levels remain unsupported until a later documented milestone deliberately raises that limit.

No inode layout, node size, magic, checksum, entry size or node version changes are introduced by level 3. `level == 0` is a leaf; every internal node must point to children whose level is exactly one lower. Parent/child logical ranges must be monotonic, non-empty and bounded.

## Runtime-verified level-3 lookup and growth

Workflow `36999077531` verified bounded common lookup through levels 0/1/2/3:

`[aurorafs-v2] bounded unified level-3 tree + inode lookup self-test passed on 512/4096-byte devices`

Main workflow `37000379420` and AHCI workflow `37000379412` verified full level-2 → level-3 structural growth at mapping 2,000,377:

`[aurorafs-v2] full level-2 root COW growth to level-3 at 2000377th extent self-test passed on 512/4096-byte devices`

Main workflow `37001092435` and AHCI workflow `37001092510` verified persistent inode publication and reopen lookup:

`[aurorafs-v2] persistent full level-2 to level-3 growth + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

## Runtime-verified continued append under existing level-3

`aurora_fs_v2_extent_tree_append_level3_cow()` replaces only the final leaf/level-1/level-2 path and clones the level-3 root, flushing children before parents.

Main workflow `37001773924` and AHCI workflow `37001774112` verified:

`[aurorafs-v2] existing level-3 root COW append through final leaf/level-1/level-2/root replacement self-test passed on 512/4096-byte devices`

The persistent composed path `aurora_fs_v2_inode_append_level3_cow_commit()` is runtime-verified by Bootstrap Build `37004408218`, head `cacdbb7d42bc580e22d1a333e3be079c1cd53584`:

`[aurorafs-v2] persistent existing level-3 COW append + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

That milestone also fixed excessive kernel-stack use in its synthetic self-test by moving 4 KiB test buffers to static scratch storage. Production COW behavior and the on-disk format were unchanged.

## Runtime-verified full-last-leaf growth under level-3

`aurora_fs_v2_extent_tree_append_level3_full_leaf_cow()` handles the first append where the final leaf below a published level-3 root is already full. It validates the final level-2 child, delegates lower-tree growth to the verified `aurora_fs_v2_extent_tree_append_level2_full_leaf_cow()`, then clones and flushes the replacement level-3 root.

Bootstrap Build `37005018110`, head `073302a537cbd31a2346f9b4ba20381091c085b8`, verified:

`[aurorafs-v2] level-3 full-last-leaf COW append with new child leaf self-test passed on 512/4096-byte devices`

The same run preserved the operational v1 path: first ATA boot created the persistent file and second ATA boot reopened it.

## Runtime-verified persistent full-last-leaf publication

`aurora_fs_v2_inode_append_level3_full_leaf_cow_commit()` composes the verified structural builder with `aurora_fs_v2_inode_publish_extent_root_cow()`. The self-test seeds a persistent inode, snapshots the old root, publishes the replacement hierarchy, reopens allocator/inode state, verifies root/count/size/allocation/generation, resolves both historical and new mappings, and confirms the old root remains byte-identical.

Aurora OS Bootstrap Build **`37005580767`**, head `fa55ab563db4a501b2b964f0db3514443efeb559`, completed successfully. The exact gate appears on q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] persistent level-3 full-last-leaf COW append + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

The same run reaches `[aurorafs] persistent file created` on first ATA boot, `[aurorafs] persistent file reopened` on second ATA boot, and M1 successfully in both paths. This confirms the synthetic v2 gate does not disturb the operational AuroraFS v1 persistence path.

All large 4 KiB synthetic buffers in the recent level-3 tests use static scratch storage rather than the kernel boot stack.

## Failure and recovery behavior

The policy remains child-first and publication-last. A failure before inode publication leaves the old root authoritative and may leak newly allocated metadata. A successful inode publication makes the replacement hierarchy authoritative. Durable reclamation, transaction replay and crash recovery remain future work.

## Next structural gate

The next level-3 mutation gate is a **full final level-1 child** below the published level-3 root. The operation must create a new level-1 sibling inside the final level-2 node, then clone/flush the replacement level-2 and level-3 parents before inode publication. The old published hierarchy must remain byte-identical until publication.

Synthetic 4096-byte logical-block verification is not physical 4Kn hardware certification.
