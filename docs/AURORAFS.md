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
- Production AuroraFS uses 64-bit sizes, block addresses and object identifiers.
- Production AuroraFS must support very large disks and files without fixed
  bootstrap-style file-size or volume-size ceilings.
- Production limits must be architectural 64-bit limits, not small static tables.
- Sparse files, dynamic allocation, large directories and multi-extent files are
  first-class production requirements.
- The filesystem format must remain independent of the physical storage transport
  so the same filesystem can live on SATA, NVMe, USB mass storage, virtual disks
  and future block devices.

## Scale contract

"Unlimited" storage is not physically or mathematically possible. Aurora therefore
defines the production contract as *practically unbounded for contemporary and
foreseeable systems* by using 64-bit addressing throughout the storage stack.
No production API may inherit the bootstrap v1 limits of eight files or one 512-byte
block per file.

The production design must use scalable allocation structures rather than fixed
arrays. File offsets and file sizes are `uint64_t`; filesystem block addresses and
volume block counts are `uint64_t`; VFS and block-device APIs must preserve those
widths end-to-end.

## Relationship with foreign filesystems

AuroraFS is Aurora's preferred native filesystem, but Aurora OS is not restricted
to AuroraFS. External storage is handled through the VFS filesystem-driver layer.
A filesystem implementation registers a probe/mount/read/write capability set and
the VFS chooses the matching driver for a discovered volume.

The target compatibility set includes at minimum:

- FAT12/FAT16/FAT32;
- exFAT;
- NTFS;
- ext2/ext3/ext4;
- XFS;
- Btrfs;
- ISO 9660;
- UDF;
- APFS and HFS+ where technically and legally practical;
- removable-media and optical filesystem variants used in the field.

Support is capability-based per driver. A driver may initially be read-only and
later gain safe write support. Unknown filesystems must never be formatted or
modified automatically.

Aurora cannot literally guarantee support for every filesystem ever created,
including undocumented, encrypted, proprietary or future formats. Instead the
architecture guarantees that filesystem support is extensible: adding a new
filesystem must not require redesigning the kernel, block layer or VFS.

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
