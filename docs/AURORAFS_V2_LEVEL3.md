# AuroraFS v2 — Level-3 extent-tree contract

Status: **level-3 bounded lookup, full level-2 → level-3 structural COW growth, and persistent inode publication + reopen lookup are runtime-verified**.

This document extends the existing AuroraFS v2 `AUREXT2` extent-tree contract. The on-disk behavior is explicit and remains compatible with the existing 4 KiB node and 256-byte inode layouts.

## Why level 3 is required

Each 4 KiB `AUREXT2` node contains a 64-byte header and 126 32-byte entries.

- level 0 leaf: up to 126 extent mappings;
- level 1: up to 126 leaf children = 15,876 mappings;
- level 2: up to 126 level-1 children = **2,000,376 mappings**;
- level 3: up to 126 level-2 children = **252,047,376 mappings**.

The first mapping that cannot fit below a completely full level-2 root is mapping **2,000,377**.

## On-disk compatibility

No inode-layout, node-size, magic, checksum, entry-size or node-version change is introduced merely to represent level 3.

The existing node header stores `level` as a 16-bit integer. AuroraFS v2 defines:

- `level == 0`: leaf node; entries map logical ranges directly to physical filesystem blocks.
- `level > 0`: internal index node; every entry points to one child whose level is exactly `level - 1`.
- Internal entry `logical_block` and `block_count_or_span` describe the logical range covered by the child subtree.
- Child ranges must be monotonic, non-empty and bounded by the parent range.
- A child whose level does not decrease by exactly one is rejected.

The current supported maximum root level is explicitly bounded to **3**. Higher values remain unsupported until a later documented milestone deliberately raises that limit.

## Runtime-verified bounded lookup

Workflow **`36999077531`** completed successfully. Its q35/AHCI, ATA first-boot and ATA persistence-boot serial paths contain:

`[aurorafs-v2] bounded unified level-3 tree + inode lookup self-test passed on 512/4096-byte devices`

The common resolver performs bounded iterative descent across levels 0/1/2/3, validates node CRC/header/range data, enforces exact parent→child level decrement and resolves both direct tree and inode-backed lookups through the same API.

## Runtime-verified full level-2 root → level-3 COW promotion

A full level-2 root contains 126 full level-1 children and covers 2,000,376 one-block mappings. `aurora_fs_v2_extent_tree_grow_level2_full_root_cow()` handles mapping 2,000,377 by:

1. validating the old full level-2 root, its final full level-1 child and final full leaf;
2. allocating, writing and flushing a fresh one-entry leaf;
3. allocating, writing and flushing a fresh one-child level-1 node;
4. allocating, writing and flushing a fresh one-child level-2 node;
5. creating a fresh level-3 root whose first child is the already-published full old level-2 root and whose second child is the new level-2 subtree;
6. flushing the new root while leaving the old hierarchy untouched.

Main workflow **`37000379420`** and AHCI workflow **`37000379412`** completed successfully. The exact gate appears in q35/AHCI and both ATA boot paths:

`[aurorafs-v2] full level-2 root COW growth to level-3 at 2000377th extent self-test passed on 512/4096-byte devices`

## Runtime-verified persistent level-3 inode publication

`aurora_fs_v2_inode_append_level3_grow_cow_commit()` composes the verified structural builder with the verified `aurora_fs_v2_inode_publish_extent_root_cow()` publication primitive.

Main workflow **`37001092435`** and AHCI workflow **`37001092510`** completed successfully. The exact gate appears on q35/AHCI, ATA first boot and ATA persistence boot:

`[aurorafs-v2] persistent full level-2 to level-3 growth + inode publication + reopen lookup self-test passed on 512/4096-byte devices`

The persistent self-test starts with an inode containing 2,000,376 one-block extent mappings under a full level-2 root, appends mapping 2,000,377, publishes the fresh level-3 root, reinitializes allocator state, rereads the inode and verifies:

- a different persisted root;
- `extent_count == 2,000,377`;
- updated `size` and `allocated_bytes`;
- generation increment;
- lookup of the newly appended mapping;
- lookup of the historical final mapping through the old level-2 branch retained beneath the new level-3 root.

The harness remains bounded: historical metadata is generated deterministically on read and only bitmap, inode and newly written COW blocks are stored.

## Failure and recovery behavior

The policy remains child-first and publication-last. A failure before inode publication leaves the old root authoritative and may leak newly allocated metadata. A successful publication makes the level-3 hierarchy authoritative; durable reclamation and transaction replay remain future work.

## Next gate

Continue COW append beneath an already-published level-3 root by replacing only its final path (`leaf → level-1 → level-2 → level-3`) before inode publication. The first target is mapping **2,000,378** while the new final leaf still has spare capacity.

Synthetic 4096-byte logical-block verification is not physical 4Kn hardware certification.
