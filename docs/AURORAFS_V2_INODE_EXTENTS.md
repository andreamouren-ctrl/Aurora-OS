# AuroraFS v2 — Persistent inode extents and hierarchical extent trees

Status: **persistent COW extent growth through a full 15,876-mapping level-1 subtree is runtime-verified; persistent full-level1 sibling publication below level-2 is runtime-verified; the level-3 on-disk semantics are defined and bounded lookup support is implemented pending runtime verification**.

This document is part of the AuroraFS v2 on-disk contract. Repository implementation and runtime verification are tracked separately.

## Stable on-disk objects

AuroraFS v2 inodes remain 256 bytes and retain four inline extent slots. Extent-tree nodes remain 4 KiB, with a 64-byte checksummed header and 32-byte entries. The node magic remains `AUREXT2`.

No level-3 work changes the inode size, node size, magic, checksum algorithm, entry layout or node version.

## Extent-tree level semantics

The existing node header stores `level` as a 16-bit integer. Its canonical meaning is now explicit:

- `level == 0`: leaf node; entries map logical ranges directly to physical filesystem blocks.
- `level > 0`: internal node; each entry points to a child node whose level is **exactly one lower**.
- Internal entries describe the logical start and span covered by the child subtree.
- Child ranges must be non-empty, ordered and bounded by the parent node range.
- A resolver must reject a child whose level does not equal `parent.level - 1`.
- A resolver must reject root levels above the implementation's documented maximum even though the on-disk field itself is wider.

The current implementation target explicitly supports roots through `level == 3`. Deeper values remain unsupported until a later documented milestone raises that bound.

## Capacity

A node contains 126 entries.

- leaf / level 0: 126 extent mappings;
- level 1: 126 leaves = **15,876 mappings**;
- level 2: 126 level-1 children = **2,000,376 mappings**;
- level 3: 126 level-2 children = **252,047,376 mappings**.

These figures assume one extent-tree entry per mapping. Multi-block extents can cover more logical filesystem blocks than the entry count alone suggests.

## Runtime-verified promotion history

The following structural and persistent transitions are runtime-verified on synthetic 512-byte and 4096-byte logical-block devices:

- inline-to-tree publication at the fifth extent — workflow `36982594135`;
- post-promotion sixth-extent COW append — `36983175716`;
- 126→127 full-leaf growth — `36983609681`;
- persistent publication of extent 127 — `36984123103`;
- continued level-1 append and persistent extent 128 — `36984773089`;
- full-last-leaf growth and persistent extent 253 — `36986548697`;
- full level-1 root → level-2 promotion and persistent extent 15,877 — `36988686741`;
- unified level-0/1/2 lookup — `36989469332`;
- continued COW append beneath an existing level-2 root — `36990022065`;
- durable inode replacement-root publication — `36992375163`;
- end-to-end level-2 append + inode publication + reopen lookup — `36994012534`;
- full-last-leaf structural growth under level-2 — `36994521731`;
- persistent full-last-leaf publication — `36996338607`;
- full final level-1 sibling growth under level-2 — `36997766355` (with AHCI coverage in `36997766251`);
- persistent publication of that full-level1 sibling growth — main workflow `36998412124`, with AHCI workflow `36998412086`.

The exact persistent sibling publication gate is:

`[aurorafs-v2] persistent level-2 full-level1 sibling growth + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

## Persistent COW ordering

AuroraFS v2 metadata mutation follows child-first, publication-last ordering:

1. validate the currently published hierarchy and new extent geometry;
2. allocate replacement/new child metadata;
3. write and flush each child before any parent that points to it;
4. write and flush the replacement root;
5. verify the inode still references the expected old root;
6. update the inode root/counters/size/allocation/generation;
7. flush the inode-table update.

The old published hierarchy is never modified in place by these extent-tree growth paths.

A failure before inode publication can leak fresh metadata but leaves the old tree authoritative. A failure after successful inode publication makes the new tree authoritative while old blocks may remain allocated. Durable reclamation and transaction replay are intentionally deferred to the crash-consistency/recovery layer.

## Bounded unified resolver

`aurora_fs_v2_extent_tree_lookup_unified()` is being generalized from the previous level-2-specific delegation into one bounded iterative resolver.

For each internal hop it validates:

- node magic/version/CRC;
- supported root depth;
- ordered, non-empty entry ranges;
- requested logical block inside the selected range;
- child block bounds;
- child node CRC/range;
- exact level decrement from parent to child.

`aurora_fs_v2_inode_extent_lookup_unified()` reads the persistent inode root and uses the same tree resolver, so level-3 support does not introduce a parallel public lookup API.

Repository implementation for a synthetic level-3 lookup gate is present. Runtime promotion requires a green CI serial line:

`[aurorafs-v2] bounded unified level-3 tree + inode lookup self-test passed on 512/4096-byte devices`

Until that exact gate is observed, level-3 lookup remains **implemented but not runtime-verified**.

## Full level-2 → level-3 growth contract

A completely full level-2 root represents 126 full level-1 children, or **2,000,376 mappings**. Mapping 2,000,377 cannot fit under that root.

The documented transition is COW-only:

1. preserve the existing full level-2 root unchanged;
2. create and flush a fresh leaf for the appended extent;
3. create and flush a fresh one-child level-1 node;
4. create and flush a fresh one-child level-2 subtree;
5. create a fresh level-3 root with two ranges: the old full level-2 root and the new level-2 subtree;
6. flush the level-3 root;
7. publish it through the inode only after the hierarchy is durable.

The detailed contract and verification plan are in `docs/AURORAFS_V2_LEVEL3.md`.

## Test-memory policy

Large structural boundaries must not be represented by giant permanent test buffers in kernel BSS. Full historical trees may be generated deterministically on block reads while only bitmap/inode state and freshly written COW metadata are retained.

This policy already prevented the 15,876-mapping full-level1 test from bloating kernel static memory and applies even more strongly to the 2,000,376-mapping level-2 boundary.

## Verification limits

All cited 4096-byte logical-block gates use synthetic QEMU devices. They validate logical-block-safe code paths but are not physical 4Kn hardware certification.
