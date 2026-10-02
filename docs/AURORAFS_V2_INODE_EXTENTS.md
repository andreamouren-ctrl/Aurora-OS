# AuroraFS v2 — Persistent inode extents and hierarchical extent trees

Status: **persistent COW extent growth through level-3 root publication is runtime-verified; continued append below an already-published level-3 root is the active mutation gate**.

This document is part of the AuroraFS v2 on-disk contract. Repository implementation and runtime verification are tracked separately.

## Stable on-disk objects

AuroraFS v2 inodes remain 256 bytes and retain four inline extent slots. Extent-tree nodes remain 4 KiB, with a 64-byte checksummed header and 32-byte entries. The node magic remains `AUREXT2`.

No level-3 work changes the inode size, node size, magic, checksum algorithm, entry layout or node version.

## Extent-tree level semantics

The canonical node-level meaning is:

- `level == 0`: leaf; entries map logical ranges to physical filesystem blocks.
- `level > 0`: internal node; each child must have level exactly `parent.level - 1`.
- Internal entries describe the logical start and span covered by the child subtree.
- Child ranges must be non-empty, ordered and bounded by the parent range.
- Root levels above the implementation's documented maximum are rejected.

The current supported maximum root level is **3**.

## Capacity

A node contains 126 entries.

- level 0: 126 mappings;
- level 1: **15,876 mappings**;
- level 2: **2,000,376 mappings**;
- level 3: **252,047,376 mappings**.

These figures count extent mappings; multi-block extents can cover more logical filesystem blocks.

## Runtime-verified promotion history

The following structural and persistent transitions are runtime-verified on synthetic 512-byte and 4096-byte logical-block devices:

- inline-to-tree publication at the fifth extent — `36982594135`;
- sixth-extent COW append — `36983175716`;
- 126→127 full-leaf growth — `36983609681`;
- persistent extent 127 — `36984123103`;
- continued level-1 append and persistent extent 128 — `36984773089`;
- full-last-leaf growth and persistent extent 253 — `36986548697`;
- full level-1 → level-2 promotion and persistent extent 15,877 — `36988686741`;
- level-0/1/2 unified lookup — `36989469332`;
- continued COW append below level-2 — `36990022065`;
- durable inode replacement-root publication — `36992375163`;
- level-2 append + inode publication + reopen — `36994012534`;
- full-last-leaf growth under level-2 — `36994521731`;
- persistent full-last-leaf publication — `36996338607`;
- full final level-1 sibling growth — `36997766355`, with AHCI coverage in `36997766251`;
- persistent full-level1 sibling publication — `36998412124`, with AHCI coverage in `36998412086`;
- bounded unified lookup through level-3 — `36999077531`;
- full level-2 root → level-3 structural COW growth at mapping 2,000,377 — main `37000379420`, AHCI `37000379412`;
- persistent level-3 root publication + reopen lookup — main `37001092435`, AHCI `37001092510`.

## Persistent COW ordering

AuroraFS v2 metadata mutation follows child-first, publication-last ordering:

1. validate the published hierarchy and new extent geometry;
2. allocate replacement/new child metadata;
3. write and flush every child before its parent;
4. write and flush the replacement root;
5. verify the inode still references the expected old root;
6. update inode root, counters, size, allocation and generation;
7. flush the inode-table update.

The old published hierarchy is not modified in place by these growth paths. A pre-publication failure can leak fresh metadata while leaving the old tree authoritative. Durable reclamation and transaction replay remain future work.

## Bounded unified resolver

`aurora_fs_v2_extent_tree_lookup_unified()` performs bounded iterative traversal through levels 0/1/2/3. It validates node magic/version/CRC, logical ranges, child bounds and exact parent→child level decrement. `aurora_fs_v2_inode_extent_lookup_unified()` uses the same resolver from the persistent inode root.

Workflow `36999077531` contains the exact runtime gate:

`[aurorafs-v2] bounded unified level-3 tree + inode lookup self-test passed on 512/4096-byte devices`

## Runtime-verified full level-2 → level-3 structural growth

A completely full level-2 root represents **2,000,376 mappings**. `aurora_fs_v2_extent_tree_grow_level2_full_root_cow()` handles mapping 2,000,377 by retaining the old full level-2 tree as the first child of a fresh level-3 root and building a new `leaf → level-1 → level-2` branch for the appended extent.

Main workflow `37000379420` and AHCI workflow `37000379412` runtime-verify this structural boundary.

## Runtime-verified persistent level-3 publication

`aurora_fs_v2_inode_append_level3_grow_cow_commit()` composes the verified level-3 structural builder with `aurora_fs_v2_inode_publish_extent_root_cow()`.

Main workflow `37001092435` and AHCI workflow `37001092510` contain the exact gate:

`[aurorafs-v2] persistent full level-2 to level-3 growth + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

The persistent test appends mapping 2,000,377, publishes the level-3 root, reopens allocator/inode state, verifies counters and generation, then resolves both the appended mapping and the historical final mapping through the retained old level-2 branch.

## Active mutation gate

The next mutation is mapping **2,000,378** under an already-published level-3 root. The final path has spare leaf capacity, so the operation should clone only:

`final leaf → final level-1 → final level-2 → level-3 root`

Each replacement child must be durable before its replacement parent is written, and the old level-3 root must remain untouched until inode publication.

## Test-memory policy

Large structural boundaries must not be represented by giant permanent BSS buffers. Historical metadata may be generated deterministically on read while only bitmap/inode state and freshly written COW metadata are retained.

## Verification limits

All cited 4096-byte logical-block gates use synthetic QEMU devices. They validate logical-block-safe code paths but are not physical 4Kn hardware certification.
