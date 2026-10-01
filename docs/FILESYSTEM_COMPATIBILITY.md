# Aurora OS Filesystem Compatibility

Status: active development.

Aurora OS uses a filesystem-agnostic storage architecture. AuroraFS is the
native filesystem, but removable and fixed media are not required to use it.
Filesystem support is implemented through independent drivers registered with
the filesystem driver registry and mounted through the common mount manager.

## Architecture

Storage path:

`hardware driver -> block device -> partition manager -> filesystem detector -> filesystem driver -> mount manager -> VFS -> applications`

The core filesystem contract uses 64-bit offsets and file sizes. Individual
foreign filesystems may impose their own on-disk limits; Aurora must not add
smaller artificial limits on top of those formats.

Aurora distinguishes four states:

- **Detected**: Aurora can identify the on-disk format.
- **Read-only**: files/directories can be mounted and read safely.
- **Read-write**: mutation is enabled only after corruption-focused testing.
- **CI verified**: a filesystem image produced by external tooling is mounted
  and exercised by Aurora in QEMU.

Recognition alone must never be presented as full filesystem support.

## Current matrix

| Filesystem | Detected | Read-only | Read-write | External-image CI |
| --- | --- | --- | --- | --- |
| AuroraFS bootstrap v1 | Yes | Yes | Bootstrap-only | Yes, reboot persistence |
| FAT32 / VFAT | Yes | Yes | No | In validation |
| exFAT | Yes | Implemented, validation pending | No | Pending |
| FAT12 | Yes | Pending | No | Pending |
| FAT16 | Yes | Pending | No | Pending |
| NTFS | Yes | Pending | No | Pending |
| ext2/ext3/ext4 | Yes | Pending | No | Pending |
| XFS | Yes | Pending | No | Pending |
| Btrfs | Yes | Pending | No | Pending |
| ISO9660 | Yes | Pending | N/A | Pending |
| UDF | Yes | Pending | No | Pending |
| HFS+ | Yes | Pending | No | Pending |
| APFS | Yes | Pending | No | Pending |

## FAT32 / VFAT

Current driver capabilities:

- read-only mount;
- nested directories;
- `stat`;
- directory enumeration;
- multi-cluster file reads;
- 64-bit API offsets;
- VFAT Long File Name sequence validation;
- VFAT short-name checksum validation;
- UTF-16 to UTF-8 conversion;
- surrogate-pair handling;
- UTF-8 names sized to preserve the maximum VFAT name without truncation.

FAT32 itself stores file length in 32 bits, so its approximately 4 GiB per-file
limit is a property of FAT32, not an Aurora VFS limitation.

## exFAT

Current driver implementation includes:

- read-only probe and mount;
- 64-bit `DataLength` and `ValidDataLength` handling;
- file-entry set checksum validation;
- Unicode filename decoding;
- directory and nested-path traversal;
- FAT-chain files;
- contiguous `NoFatChain` files;
- 64-bit file offsets and lengths at the Aurora interface.

It remains marked validation-pending until an image created by independent
exFAT tooling is exercised in CI.

## Write-safety policy

Foreign filesystem write support is never enabled merely because a parser can
read the format. Before a driver may advertise read-write access it must have:

1. externally generated test images;
2. create/modify/delete tests;
3. remount and reboot persistence tests;
4. dirty-volume handling tests where the format supports them;
5. malformed/corrupt metadata rejection tests;
6. boundary-size and allocation tests;
7. cross-validation by standard tools from another operating system.

Until those conditions are met, Aurora mounts that format read-only.

## Long-term target

Aurora should automatically discover storage devices, enumerate MBR/GPT
partitions, identify every supported filesystem, and mount it through the same
VFS namespace without forcing conversion to AuroraFS.

New filesystem support must remain modular: adding a driver must not require a
redesign of the kernel, block layer, partition manager, mount manager, or VFS.
