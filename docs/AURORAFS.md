# AuroraFS

Status: **AuroraFS v2 active read-write system filesystem; bounded Level-3 hierarchy complete/runtime verified**

AuroraFS is Aurora OS's native persistent filesystem.

AuroraFS v1 remains a compatibility/bootstrap format. Current system-filesystem development and the real `/system` mount use the separately versioned **AuroraFS v2** on-disk format.

## Versioning policy

AuroraFS formats are explicit and are never silently reinterpreted:

- **v1**: `AURAFS1\0`, bootstrap/compatibility format with a 512-byte logical filesystem block and deliberately small limits;
- **v2**: `AURAFS2\0`, 4 KiB filesystem-block production-direction format with 64-bit metadata fields, scalable allocation and bounded extent trees.

No v1 volume is automatically rewritten as v2. Any future migration tool must perform an explicit, recoverable conversion.

## Current AuroraFS v2 capabilities

The live v2 path includes:

- checksummed v2 superblock;
- 4 KiB logical filesystem blocks independent from transport block size;
- 64-bit object identifiers, file sizes, offsets and block coordinates;
- scalable multi-block allocation bitmap;
- nested directories and checksummed directory records;
- four inline extents per inode;
- bounded external extent trees through Level 3;
- copy-on-write structural growth and durable inode-root publication;
- general create and mkdir;
- file read/write and truncate;
- remove and rename;
- free-space reclamation;
- crash-consistent namespace transaction records;
- recovery after interrupted namespace mutations;
- metadata integrity/corruption gates;
- ownership, mode and timestamps;
- persistent ACLs;
- explicit `fsync`, `fdatasync` and global `sync` durability semantics;
- real mount through the common filesystem driver/mount/VFS stack at `/system`;
- two-boot persistence regression coverage.

## Extent hierarchy

The public v2 extent hierarchy is intentionally bounded at Level 3.

The closed hierarchy contract is documented in:

- [`AURORAFS_V2_LEVEL3_COMPLETION.md`](AURORAFS_V2_LEVEL3_COMPLETION.md)

The current supported mapping capacities are:

- Level 0 / leaf: **126 extents**
- Level 1: **15,876 extents**
- Level 2: **2,000,376 extents**
- Level 3: **252,047,376 extents**

The next mapping beyond the Level-3 bound is rejected without partially mutating the filesystem.

Level 4 is intentionally not part of the current on-disk contract. Increasing the maximum would require a separate documented format milestone.

## Mutation and durability model

AuroraFS v2 does not treat a successful in-memory metadata edit as durable completion.

The current write path separates:

1. allocation / replacement-tree preparation;
2. durable publication of replacement metadata;
3. inode/root publication;
4. namespace transaction completion;
5. explicit device flush where required by the durability contract.

The VFS exposes explicit durability semantics through `fsync`, `fdatasync` and `sync`.

If corruption or an unrecoverable transaction ambiguity is detected, the path fails closed rather than silently continuing with guessed metadata.

## Namespace operations

The current v2 namespace path supports:

- create;
- mkdir;
- remove;
- rename.

Mutation is designed to be non-partial across the transaction/recovery boundary: the filesystem either recovers the previously valid namespace state or completes the new committed state according to the recorded transaction.

## Metadata and security

AuroraFS v2 persists:

- ownership;
- permission/mode metadata;
- timestamps;
- ACLs.

These filesystem primitives feed Aurora's higher-level capability/session model but do not replace it. A pathname or ACL match alone does not create arbitrary kernel capability authority.

## Transport independence

AuroraFS is not tied to one storage controller.

The common stack is:

```text
ATA / AHCI / NVMe / future transport
        |
        v
block_device
        |
        v
partition view
        |
        v
AuroraFS driver
        |
        v
mount manager
        |
        v
VFS
```

ATA PIO, AHCI and NVMe regression paths are all used in QEMU validation.

Transport verification and filesystem correctness remain separate claims: a green NVMe software path does not imply every physical NVMe device has been certified.

## AuroraFS v1 compatibility role

v1 is retained because it established the first persistent Aurora filesystem and provides useful backward/regression coverage.

Its small fixed-layout constraints must not be presented as v2 limitations.

v1 is not the current production-direction feature baseline.

## Current limits

AuroraFS v2 is a serious experimental native filesystem, but it is not yet equivalent in maturity to ext4, XFS, Btrfs, NTFS or ReFS.

Important remaining work includes:

- sparse-file semantics;
- explicit v1 -> v2 migration tooling;
- mature offline inspection/repair tooling;
- quotas;
- broader long-duration concurrency/stress testing;
- TRIM/discard policy;
- production defragmentation strategy;
- encryption/compression integration where justified;
- snapshot/reflink policy if adopted;
- broader physical-disk failure and power-loss testing;
- fuzzing and corruption-injection expansion;
- performance benchmarking/tuning at large directory/file counts.

## Design goals

AuroraFS continues to follow these rules:

- native format, not a clone of FAT/ext/NTFS;
- explicit format versioning;
- transport independence;
- 64-bit production metadata;
- integrity validation before trusting metadata;
- fail-closed behavior on ambiguous corruption;
- bounded memory use during metadata traversal;
- recoverable mutation;
- explicit durability;
- no automatic formatting of unknown disks;
- no silent format expansion beyond documented bounds.

## Verification status terminology

Aurora documentation uses three distinct terms:

- **designed** — contract exists;
- **implemented** — code path exists;
- **runtime verified** — executable CI/QEMU path exercised the behavior.

Runtime verification is not equivalent to real-hardware certification.

## Related documents

- [`FILESYSTEM_COMPATIBILITY.md`](FILESYSTEM_COMPATIBILITY.md)
- [`AURORAFS_V2_LEVEL3_COMPLETION.md`](AURORAFS_V2_LEVEL3_COMPLETION.md)
- [`ROADMAP.md`](ROADMAP.md)
- [`SYSTEM_AUDIT_2026-10-07.md`](SYSTEM_AUDIT_2026-10-07.md)
