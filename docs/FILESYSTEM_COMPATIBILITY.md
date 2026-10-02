# Aurora OS Filesystem Compatibility

Status: **active development**.

Aurora OS uses a filesystem-agnostic storage architecture. AuroraFS is the native filesystem direction, but removable and fixed media are not required to use it. Filesystems are implemented as independent drivers registered with the common filesystem-driver registry and mounted through the common mount manager.

## Architecture

Storage path:

`hardware transport -> block device -> partition manager -> filesystem detector -> filesystem driver -> mount manager -> VFS -> applications`

The common filesystem contract uses 64-bit offsets and file sizes. Individual filesystems may impose their own on-disk limits; Aurora must not add smaller artificial limits on top of those formats.

Aurora distinguishes four support states:

- **Detected**: Aurora can identify the on-disk format.
- **Read-only**: files/directories can be mounted and read safely.
- **Read-write**: mutation is enabled only after corruption-focused validation.
- **CI verified**: a runtime test exercises the format and common VFS path in QEMU.

Recognition alone must never be presented as full filesystem support.

## Current matrix

| Filesystem | Detected | Read-only | Read-write | CI verification |
| --- | --- | --- | --- | --- |
| AuroraFS bootstrap v1 | **Yes** | **Yes** | **Existing bootstrap files only** | **Yes — reboot persistence + common VFS** |
| FAT32 / VFAT | Yes | **Yes** | No | **Yes — including Unicode LFN** |
| exFAT | Yes | **Yes** | No | **Yes on 512-byte transport; 4Kn runtime test pending** |
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

The combined storage validation baseline was green in workflow run **#285** (`36892511779`) at commit `445abb2246951b13bb5aae291713b1eeb5aa0420`. Subsequent storage changes are tracked independently until each new runtime gate is green.

## Runtime-verified paths

The current storage smoke test exercises these chains end-to-end:

`ATA PIO -> whole-device AuroraFS volume -> AuroraFS driver -> mount manager -> VFS -> /system/aurora.boot-probe`

and:

`ATA PIO -> MBR -> partition scan -> filesystem driver selection -> mount manager -> VFS -> file read`

The CI disk contains:

- a signed AuroraFS bootstrap area used for persistence validation and common VFS mounting at `/system`;
- partition 1: FAT32/VFAT created with standard external tooling;
- partition 2: exFAT created with standard external tooling.

The test performs two complete QEMU boots against the same disk image.

Workflow **#315** (`36962807514`) demonstrated a complete successful runtime path for ATA signature read, PIO write, CACHE FLUSH, readback, byte-for-byte comparison, AuroraFS mount/persistence, FAT32/VFAT, exFAT, and common VFS routing. Its overall workflow result was red only because the YAML still expected the obsolete pre-flush success message. Commit `8c493274fb81fbc6bc83d4856efcd19218775e27` fixes that assertion and adds explicit checks for AuroraFS mounting at `/system` and mounted-path VFS routing. The transport integration is therefore runtime-demonstrated, while the corrected CI gate remains pending until its run closes green.

## Partition-sector handling

The partition parser accepts logical block sizes of 512, 1024, 2048, and 4096 bytes. MBR metadata is interpreted from the standard first 512 bytes of LBA 0, while GPT header and entry-table addressing use the block device's actual logical block size.

GPT entries that cross a logical-block boundary are assembled from adjacent blocks. GPT header CRC32, partition-entry-array CRC32, and primary-to-backup GPT fallback are implemented.

The partition layer is **runtime-verified on both 512-byte and 4096-byte logical-block synthetic devices**. This does not yet constitute complete end-to-end 4Kn support: every filesystem and transport must be independently validated on non-512 logical blocks.

## AuroraFS bootstrap v1

Runtime-verified common-driver capabilities:

- filesystem probe;
- common registry registration;
- common mount manager integration;
- mount at `/system` through a synthetic whole-device partition view;
- root-directory `stat`;
- root-directory enumeration;
- file reads through the VFS;
- writes to existing bootstrap files;
- persistence verification across reboot.

Current write support does not imply a production filesystem. AuroraFS bootstrap v1 still lacks general create/remove operations through the filesystem-driver contract, scalable allocation, nested directories, multi-block files, sparse files, extents, crash-consistent transactions, and 512e/4Kn-safe parsing.

## FAT32 / VFAT

Runtime-verified driver capabilities:

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
- UTF-8 name buffers large enough to preserve the maximum VFAT filename;
- reading a file with a Unicode long filename generated by external tooling;
- file access through the common VFS mount path.

FAT32 stores file length in 32 bits, so its approximately 4 GiB per-file ceiling is a FAT32 format property, not an Aurora VFS limit.

FAT32 logical-block hardening is still pending; existing runtime verification uses the 512-byte ATA compatibility transport.

## exFAT

Runtime-verified read-only support on the existing 512-byte transport includes:

- probe and mount;
- 64-bit `DataLength` and `ValidDataLength` handling;
- file-entry set checksum validation;
- Unicode filename decoding;
- directory and nested-path traversal;
- FAT-chain files;
- contiguous `NoFatChain` files;
- 64-bit file offsets and lengths at the Aurora interface;
- reading a file from an externally generated exFAT image;
- file access through the common VFS mount path.

Logical-block hardening is now implemented in the exFAT driver:

- boot-record reads use a buffer sized for device blocks up to 4096 bytes;
- the previous `device->block_size == 512` requirement has been removed;
- exFAT filesystem sector size and device logical-block size are tracked independently;
- mount/probe reject incompatible geometry rather than reading past a 512-byte buffer;
- the generic byte-reading path continues to translate filesystem byte offsets through the actual device block size.

This change is **implemented but not yet runtime-verified on a 4096-byte logical-block exFAT device**. A dedicated synthetic 4Kn exFAT test is the next validation gate.

## Current transport limitations

Filesystem support and hardware transport support are separate concerns.

Currently:

- ATA PIO provides the disk read/write compatibility transport used in the integration CI and exposes 512-byte sectors;
- the block layer has a device registry and optional explicit flush contract; ATA PIO exposes CACHE FLUSH and the full read/write/flush path has been runtime-demonstrated in workflow #315, with the corrected green CI gate pending;
- AHCI controller discovery and ABAR probing exist, but AHCI data I/O is not implemented yet;
- NVMe is not implemented yet;
- USB/xHCI and USB mass-storage are not implemented yet;
- partition parsing is runtime-verified for 512-byte and 4096-byte logical blocks;
- exFAT is logically hardened for device blocks up to 4096 bytes but awaits dedicated 4Kn runtime verification;
- FAT32 and AuroraFS still contain remaining filesystem-specific 512-byte assumptions.

Therefore FAT32/exFAT/AuroraFS driver support being verified does **not** yet mean Aurora can access every modern physical SATA/NVMe/USB device.

## Write-safety policy

Foreign filesystem write support is never enabled merely because a parser can read the format. Before a foreign driver may advertise read-write access it must have:

1. externally generated test images;
2. create/modify/delete tests;
3. remount and reboot persistence tests;
4. dirty-volume handling tests where the format supports them;
5. malformed/corrupt metadata rejection tests;
6. boundary-size and allocation tests;
7. cross-validation by standard tools from another operating system.

Until those conditions are met, Aurora mounts that format read-only.

AuroraFS follows a separate native-format development path, but production write support must still meet equivalent integrity, persistence, corruption, and recovery requirements before being considered stable.

## Long-term target

Aurora should automatically discover storage devices, enumerate MBR/GPT partitions, identify supported filesystems, and mount them through one VFS namespace without forcing conversion to AuroraFS.

New filesystem support must remain modular: adding a driver must not require redesigning the kernel, block layer, partition manager, mount manager, or VFS.
