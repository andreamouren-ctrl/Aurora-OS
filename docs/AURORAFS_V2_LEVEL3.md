# AuroraFS v2 — Level-3 extent-tree contract

Status: **architecture defined; level-3 lookup and mutation are not yet runtime-verified**.

This document extends the existing AuroraFS v2 `AUREXT2` extent-tree contract. It is intentionally written before the level-3 implementation so the on-disk behavior is explicit rather than inferred from code.

## Why level 3 is required

Each 4 KiB `AUREXT2` node contains a 64-byte header and 126 32-byte entries.

- level 0 leaf: up to 126 extent mappings;
- level 1: up to 126 leaf children = 15,876 mappings;
- level 2: up to 126 level-1 children = **2,000,376 mappings**;
- level 3: up to 126 level-2 children = **252,047,376 mappings**.

The first mapping that cannot fit below a completely full level-2 root is mapping **2,000,377** (zero-based logical extent index 2,000,376 when each extent covers one filesystem block).

## On-disk compatibility

No inode-layout, node-size, magic, checksum, entry-size or node-version change is introduced merely to represent level 3.

The existing node header already stores `level` as a 16-bit integer, and internal entries already store a logical start, child physical block and covered logical span. AuroraFS v2 therefore defines the field semantically as follows:

- `level == 0`: leaf node. Entries map logical ranges directly to physical filesystem blocks.
- `level > 0`: internal index node. Every entry points to one child node whose level is **exactly `level - 1`**.
- An internal entry's `logical_block` and `block_count_or_span` describe the logical range covered by that child subtree.
- Child ranges within a node must be monotonic, non-empty and bounded by the parent header range.
- The parent header range must cover its published child entries.

The implementation must reject a child whose level does not decrease by exactly one. It must also reject unsupported root levels even though the on-disk field is wider.

## Supported-depth policy

AuroraFS v2 does not interpret the 16-bit level field as permission for arbitrary-depth traversal.

The implementation exposes an explicit supported maximum depth. During the level-3 milestone the maximum supported root level becomes **3**. Root levels greater than 3 remain invalid/unsupported until a later documented implementation deliberately raises that limit.

This bounded policy prevents malformed metadata from causing unbounded traversal and makes the runtime capability match the documented implementation.

## Generic lookup contract

The common extent resolver is to become a bounded iterative descent rather than a hard-coded level-0/1/2 cascade.

For a requested logical block it must:

1. load and CRC-validate the root;
2. reject a root above the supported maximum level;
3. confirm the logical block is inside the root header range;
4. while the current level is greater than zero, select exactly one entry containing the logical block;
5. validate the entry child block and load the child;
6. require `child.level + 1 == parent.level`;
7. require the child's advertised logical range to be compatible with the selected parent entry;
8. continue until level 0;
9. resolve the logical block through the leaf extent mapping.

The traversal is bounded by the supported maximum level and must not recurse without an explicit bound.

## Full level-2 root → level-3 COW promotion

A full level-2 root contains 126 full level-1 children and covers 2,000,376 one-block mappings. The next append cannot mutate that published tree in place.

The COW promotion is defined as:

1. validate the old root is level 2, contains 126 children, and its final level-1 subtree/final leaf are structurally full;
2. validate the appended extent geometry and monotonic logical range;
3. allocate, write and flush a fresh level-0 leaf containing the new extent;
4. allocate, write and flush a fresh one-child level-1 node pointing to that leaf;
5. allocate, write and flush a fresh one-child level-2 node pointing to that level-1 node;
6. allocate a fresh level-3 root with two entries:
   - entry 0 points to the already-published full old level-2 root and covers its complete 2,000,376-mapping range;
   - entry 1 points to the fresh level-2 subtree and covers the appended range;
7. write and flush the level-3 root;
8. only after the complete replacement hierarchy is durable may the inode publish the new root.

The old level-2 root and all of its descendants remain unchanged.

## Failure and recovery behavior

The current AuroraFS v2 policy remains child-first and publication-last.

A failure before inode publication leaves the old root authoritative and may leak freshly allocated metadata blocks. A failure after successful inode publication makes the new hierarchy authoritative while old metadata may remain allocated. Durable reclamation and transaction replay remain separate future work.

## Verification strategy

Tests must not materialize two million mappings or a giant synthetic disk in kernel BSS. Historical full subtrees should be generated deterministically when read, while only the allocation bitmap, inode block and newly written COW metadata are stored in the bounded test context.

Required gates, in order:

1. generic bounded resolver correctly handles existing levels 0/1/2 without regression;
2. generic resolver correctly traverses a synthetic level-3 tree on 512-byte and 4096-byte logical-block devices;
3. full level-2 → level-3 structural COW promotion is runtime-verified;
4. inode publication of the new level-3 root plus reopen lookup is runtime-verified.

Synthetic 4096-byte logical-block verification is not physical 4Kn hardware certification.
