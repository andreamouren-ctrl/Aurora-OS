# AuroraFS

Status: **bootstrap format v1 — runtime persistence and common VFS integration verified, experimental, not production-ready**.

AuroraFS is Aurora OS's native persistent filesystem direction. The current implementation is not the final production filesystem; it is the first durable on-disk contract used to validate persistent metadata and file contents across reboot and the first native filesystem implementation wired through Aurora's common filesystem-driver, mount-manager, and VFS path.

## Verified status

AuroraFS bootstrap v1 has been runtime-verified in QEMU through two complete boots against the same ATA-backed disk image. The first boot formats the signed development volume and creates `aurora.boot-probe`; the second boot validates the existing filesystem and reopens the same persistent file.

AuroraFS is now registered through the common filesystem-driver registry. After the bootstrap persistence probe, the signed development disk is exposed as a synthetic whole-device volume, mounted at `/system`, and `aurora.boot-probe` is read back through the common VFS path.

The combined storage test is green in GitHub Actions workflow **#285** (`36892511779`) at commit `445abb2246951b13bb5aae291713b1eeb5aa0420`.

This is CI/QEMU verification, not real-hardware certification.

## Common filesystem integration

The currently verified native path is:

`ATA PIO -> block device -> synthetic whole-device partition -> AuroraFS driver -> mount manager -> VFS -> /system/aurora.boot-probe`

The AuroraFS bootstrap driver currently implements:

- format probing;
- mount;
- root-directory `stat`;
- root-directory enumeration;
- file reads;
- writes to existing bootstrap files;
- common VFS routing through `/system`.

Bootstrap formatting and initial creation of the persistence probe remain deliberately separate development-only operations guarded by the signed test disk gate.

## Design goals

- Aurora-native on-disk format, not a FAT/ext clone.
- Strict separation from hardware transports and the VFS layer.
- Explicit versioning from the first persistent format.
- Metadata and data integrity checks.
- Deterministic early-bootstrap structures suitable for kernel validation.
- No automatic formatting of arbitrary disks.
- Production AuroraFS uses 64-bit sizes, block addresses, object identifiers, and offsets.
- Production AuroraFS must support very large disks and files without bootstrap-style ceilings.
- Sparse files, dynamic allocation, large directories, and multi-extent files are first-class production requirements.
- The on-disk format must remain independent of SATA, NVMe, USB mass storage, virtual disks, and future transports.

## Scale contract

"Unlimited" storage is not physically or mathematically possible. Aurora therefore defines the production goal as *practically unbounded for contemporary and foreseeable systems* by preserving 64-bit widths throughout the storage stack.

No production API may inherit bootstrap v1 limits such as eight directory entries or one 512-byte block per file.

Production allocation must use scalable structures rather than fixed arrays. File offsets and file sizes, filesystem block addresses, object identifiers, and volume block counts remain 64-bit end-to-end.

## Relationship with foreign filesystems

AuroraFS is Aurora's preferred native filesystem, but Aurora OS is not restricted to it. External storage is handled through independent filesystem drivers registered with the same common filesystem-driver registry and mounted through the same mount manager.

Current runtime-verified filesystem drivers include:

- AuroraFS bootstrap v1, read/write for existing bootstrap files and mounted at `/system`;
- FAT32/VFAT read-only, including Long File Names and Unicode decoding;
- exFAT read-only, including 64-bit `DataLength` handling.

The target compatibility set also includes FAT12/16, NTFS, ext2/3/4, XFS, Btrfs, ISO9660, UDF, and HFS+/APFS where technically and legally practical.

Support is capability-based per driver. A driver may begin read-only and later gain safe write support. Unknown filesystems must never be formatted or modified automatically.

## Development safety gate

The current AuroraFS bootstrap path is allowed to format only after the ATA test path verifies the explicit development signature:

`AURORA-STORAGE-TEST-V1`

This prevents the development persistence self-test from formatting an ordinary disk.

The signature is a CI/development gate and is **not** part of the future production filesystem format.

## Bootstrap v1 layout

The bootstrap filesystem begins at device LBA 8 so low sectors remain available to storage probes and partition metadata used by the development image.

Relative to the AuroraFS base LBA:

| Relative block | Purpose |
| --- | --- |
| 0 | Superblock |
| 1 | Fixed root directory table |
| 2..9 | Bootstrap data blocks |

All bootstrap blocks are currently 512 bytes.

### Superblock

The v1 superblock occupies exactly one block and contains:

- magic `AURAFS1\0`;
- format version `1`;
- block size;
- filesystem-visible block count;
- root directory block;
- first data block;
- generation counter;
- FNV-1a 32-bit metadata checksum;
- reserved bytes for future compatible expansion.

The checksum is calculated over the complete superblock with the checksum field set to zero.

### Root directory

Bootstrap v1 contains eight fixed-size 64-byte directory entries in a single block. Each entry contains:

- in-use flag;
- bootstrap name field;
- first data block;
- file size;
- FNV-1a 32-bit data checksum;
- reserved bytes.

The current implementation intentionally supports only the root directory and single-block files up to 512 bytes. These are bootstrap constraints only.

## Persistence probe

On the first signed development boot the kernel creates `aurora.boot-probe`. On a later boot using the same disk image AuroraFS must:

1. validate the existing superblock and checksum;
2. load the root directory;
3. locate the probe file;
4. read its data block;
5. verify the stored checksum and expected content;
6. increment and persist the generation counter;
7. mount the same volume through the common filesystem framework;
8. resolve and read `/system/aurora.boot-probe` through the VFS.

CI requires the second boot to report that the persistent file was reopened, and runtime execution must reach M1 after common VFS verification.

## Not yet production-ready

AuroraFS v1 still lacks:

- scalable allocation/free-space maps;
- directories below root;
- multi-block files and extents;
- sparse files;
- crash-consistent transactions, journaling, or copy-on-write metadata;
- free-space reclamation;
- permissions/ownership/ACL metadata;
- timestamps;
- encryption;
- snapshots;
- file creation/removal through the production filesystem-driver contract;
- corruption recovery beyond checksum rejection;
- general partition-based production mounting;
- 512e/4Kn-safe on-disk parsing.

These will be introduced deliberately. Bootstrap v1 exists to prove safe persistence before Aurora Identity is allowed to depend on durable local state.