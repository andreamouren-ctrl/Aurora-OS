# AuroraFS

Status: bootstrap format v1, experimental and intentionally minimal.

AuroraFS is Aurora OS's native persistent filesystem direction. The current
implementation is not the final production filesystem; it is the first durable
on-disk contract used to validate the complete path from a real block device to
persistent filesystem metadata and file contents across reboot.

## Design goals

- Aurora-native on-disk format, not a FAT/ext clone.
- Strict separation from hardware drivers and the VFS layer.
- Explicit versioning from the first persistent format.
- Metadata and data integrity checks in the bootstrap implementation.
- Deterministic, bounded structures suitable for early kernel validation.
- No automatic formatting of arbitrary disks during bootstrap testing.

## Safety gate

The current kernel bootstrap mounts/formats AuroraFS only after the ATA test
path has verified the explicit `AURORA-STORAGE-TEST-V1` signature in LBA 0.
This prevents the development self-test from formatting an ordinary disk.

This signature is a CI/development gate, not part of the future production
filesystem format.

## Bootstrap v1 layout

The bootstrap filesystem begins at device LBA 8 so the low sectors remain
available to storage probes and future partition metadata.

Relative to the AuroraFS base LBA:

| Relative block | Purpose |
| --- | --- |
| 0 | Superblock |
| 1 | Fixed root directory table |
| 2..9 | Bootstrap data blocks |

All bootstrap blocks are 512 bytes.

### Superblock

The v1 superblock occupies exactly one block and contains:

- magic: `AURAFS1\0`
- format version: `1`
- block size
- filesystem-visible block count
- root directory block
- first data block
- generation counter
- FNV-1a 32-bit metadata checksum
- reserved bytes for future compatible expansion

The checksum is calculated over the complete superblock with the checksum field
set to zero.

### Root directory

Bootstrap v1 contains eight fixed-size 64-byte directory entries in a single
block. Each entry contains:

- in-use flag
- UTF-8/ASCII-compatible bootstrap name field (31 characters plus terminator)
- first data block
- file size
- FNV-1a 32-bit data checksum
- reserved bytes

The current implementation intentionally supports only the root directory and
single-block files up to 512 bytes. These limits are bootstrap constraints, not
production AuroraFS goals.

## Persistence probe

The kernel creates a small `aurora.boot-probe` file on the first signed test
boot. On the next boot using the same disk image, AuroraFS must:

1. validate the existing superblock and checksum;
2. load the existing root directory;
3. locate the probe file;
4. read its data block;
5. verify the stored checksum and expected content;
6. increment and persist the filesystem generation counter.

CI performs two complete QEMU boots against the same raw disk image. The second
boot must report that the persistent file was reopened.

## Not yet production-ready

Bootstrap v1 does not yet provide:

- dynamic allocation maps;
- directories below root;
- multi-block files;
- crash-consistent transactions/journaling or copy-on-write metadata;
- free-space reclamation;
- permissions/ownership/ACL metadata;
- timestamps;
- encryption;
- snapshots;
- VFS mount integration;
- partition discovery;
- corruption recovery beyond checksum rejection.

These will be introduced deliberately as the storage architecture matures. The
bootstrap format exists to prove persistence safely before Aurora Identity is
allowed to depend on disk state.
