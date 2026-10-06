# Aurora Ring 3 Process-Owned Anonymous Memory

Status: **Identity-enabling foundation**

Aurora provides bounded anonymous user memory for trusted and ordinary Ring 3 processes through process-owned mappings. This facility exists primarily to support real user-space services that need working memory larger than the fixed user stack; Aurora Identity Argon2id is the first production consumer.

## ABI

The public syscall ABI exposes:

- `AURORA_SYS_USER_MEMORY_ALLOC`
- `AURORA_SYS_USER_MEMORY_FREE`

`ALLOC(size)` returns a page-aligned user virtual address or `AURORA_SYS_RESULT_ERROR`.

`FREE(address)` releases exactly one complete mapping previously returned by `ALLOC` for the calling process.

Mappings are never transferable between processes through this ABI.

## Ownership and permissions

Every anonymous range belongs to exactly one `aurora_process`.

Mapped pages are:

- user-accessible;
- writable;
- non-executable;
- zero-filled before first exposure.

The Process Manager records each range independently from the VMM page tables. The VMM therefore remains responsible only for translation/page-table structure; it does not guess ownership of leaf frames.

## Bounds

Current v1 limits are intentionally conservative:

- maximum 16 anonymous ranges per process;
- maximum single range: 32,768 pages / 128 MiB;
- maximum anonymous memory per process: 65,536 pages / 256 MiB;
- anonymous virtual window: `0x0000000100000000` through `0x0000000200000000`.

The single-range limit is deliberately above Aurora Identity's current Argon2id creation policy of 64 MiB (`memory_kib = 65536`) while remaining bounded.

## Secret hygiene

Pages are cleared:

1. before they are mapped into userspace;
2. before explicit `FREE` returns their frames to the PMM;
3. before process lifecycle reclamation returns unfreed anonymous frames after exit, fault or supervised restart.

This is required for Identity because Argon2 working memory and future sensitive service buffers can contain credential-derived material.

## Lifecycle integration

`process_reap()` preflights all process-owned mappings before destroying the address space. Anonymous mappings left active by a crashed or exiting service are reclaimed together with image pages, user stack pages and private page tables.

The Identity runtime test intentionally leaves one anonymous mapping allocated until process exit. The existing service-supervisor PMM baseline assertion therefore verifies that supervised restart does not leak anonymous memory.

## Runtime library

The compiled Aurora Identity runtime supplies freestanding `malloc()` and `free()` wrappers backed by these syscalls. This is sufficient for the pinned Argon2 reference implementation's default memory allocation path without exposing arbitrary physical mapping or VMM controls to Ring 3.

## Security boundaries

This API is not a general `mmap` implementation. It does not currently provide:

- caller-selected virtual addresses;
- executable mappings;
- file-backed mappings;
- shared mappings;
- capability transfer of memory objects;
- overcommit or swapping;
- resize/remap operations.

Those features, if added later, require separate ownership and authorization contracts.

## Identity relationship

This foundation removes the memory blocker for linking the real `services/identity` core and Argon2id provider into the compiled Ring 3 Identity Service. It does not by itself make Identity authentication live; the subsequent integration must wire the DRBG, Machine Secret, Aurora-native persistent store, Argon2id provider and real authentication IPC protocol into the service runtime.
