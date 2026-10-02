# AuroraFS v2 — Unified extent lookup and hierarchical COW mutation

Status: **level-2 growth/mutation and full-level1 sibling publication are runtime-verified; bounded unified level-3 lookup is implemented and awaiting runtime verification**.

This document is part of the AuroraFS v2 implementation contract. Designed, implemented and runtime-verified states are intentionally distinct.

## Runtime-verified level-2 milestones

AuroraFS v2 has runtime-verified the complete progression from a level-1 tree into continued level-2 mutation:

- full level-1 root → level-2 promotion at extent 15,877: workflow `36988686741`;
- unified level-0/1/2 lookup: `36989469332`;
- continued COW append beneath an existing level-2 root: `36990022065`;
- durable inode replacement-root publication: `36992375163`;
- end-to-end level-2 append + inode publication + reopen lookup: `36994012534`;
- full-last-leaf structural growth under level-2: `36994521731`;
- persistent full-last-leaf publication: `36996338607`;
- full final level-1 sibling growth while the level-2 root has spare capacity: `36997766355`, with AHCI coverage in `36997766251`.

The COW hierarchy is always written bottom-up and the inode is the final publication point.

## Runtime-verified persistent full-level1 sibling publication

Main workflow `36998412124` (head `aeeecc77789e4bf24c06c02ecd83d1fdf1c3e3ef`) completed successfully. The exact gate appears in q35/BIOS, ATA first boot and ATA persistence boot:

`[aurorafs-v2] persistent level-2 full-level1 sibling growth + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

AHCI workflow `36998412086` also completed successfully with the same persistent gate before the AHCI-backed filesystem traversal.

`aurora_fs_v2_inode_append_level2_full_level1_cow_commit()` composes:

1. the verified `aurora_fs_v2_extent_tree_append_level2_full_level1_cow()` structural builder;
2. durable replacement-root publication through `aurora_fs_v2_inode_publish_extent_root_cow()`.

The self-test begins with a persistent inode referencing a full 15,876-mapping level-1 subtree below a level-2 root, appends mapping 15,877, publishes the replacement root, reopens allocator/inode state, validates root/counters/size/allocation/generation, then resolves both the appended and an old mapping.

The historical 126-leaf subtree is generated deterministically on read, preventing large permanent kernel BSS while preserving the exact structural boundary.

## Level-3 architecture

A full level-2 root contains 126 full level-1 children:

`126 × 126 × 126 = 2,000,376 mappings`

The next mapping requires another tree level. The on-disk `AUREXT2` header already stores `level` as `uint16_t`, and internal entries already have a generic child pointer/range representation.

AuroraFS v2 therefore defines level 3 as an explicit use of the existing node format, not as an implicit format change:

- level 0 = extent leaf;
- level N > 0 = internal index node;
- every internal child must have exactly level N-1;
- runtime code supports only explicitly documented maximum levels and rejects deeper values.

The current milestone raises the supported resolver ceiling to level 3. The detailed contract is `docs/AURORAFS_V2_LEVEL3.md`.

## Implemented bounded unified level-3 resolver

`aurora_fs_v2_extent_tree_lookup_unified()` no longer delegates to the old fixed level-2 resolver. It now performs bounded iterative descent through roots at levels 0, 1, 2 or 3.

At each hop it validates:

- `AUREXT2` magic and node version;
- CRC32;
- non-empty bounded entry count;
- supported root level (`<= 3`);
- monotonic/non-overlapping logical ranges;
- selected child block bounds;
- child range compatibility with the parent entry;
- exact `parent.level - 1` child level.

The traversal stops only at level 0, where the selected extent resolves the physical block and contiguous count.

`aurora_fs_v2_inode_extent_lookup_unified()` now reads the inode-table block directly and invokes the same unified resolver, so level 3 does not create a parallel inode lookup API.

The synthetic test builds `level 3 → level 2 → level 1 → leaf → data`, exercises both direct tree lookup and persistent inode lookup, and runs on 512-byte and 4096-byte logical-block devices.

Expected runtime gate:

`[aurorafs-v2] bounded unified level-3 tree + inode lookup self-test passed on 512/4096-byte devices`

Until a green CI serial log contains that exact line, level-3 lookup remains **implemented but not runtime-verified**.

## Next structural mutation

After the level-3 resolver gate is green, AuroraFS v2 will implement full level-2 → level-3 COW promotion at mapping 2,000,377:

1. retain the old full level-2 root unchanged;
2. create + flush a new one-entry leaf;
3. create + flush a new one-child level-1 node;
4. create + flush a new one-child level-2 subtree;
5. create + flush a new level-3 root with two children: old full level-2 root + new level-2 subtree;
6. return the replacement level-3 root;
7. in a subsequent publication gate, publish it through the inode and verify reopen lookup.

Historical full metadata will be generated deterministically by the test block device; the test must not materialize two million mappings in kernel memory.

## Compatibility and safety

No level-3 lookup work changes the 4 KiB extent-node size, 64-byte header, 32-byte entry layout, `AUREXT2` magic or 256-byte inode layout.

The COW policy favors recoverability over immediate reclamation. Failure before inode publication can leak replacement metadata while the old hierarchy remains authoritative. Transaction replay and durable reclamation remain future work.

All 4096-byte logical-block verification described here is synthetic QEMU coverage, not physical 4Kn certification.
