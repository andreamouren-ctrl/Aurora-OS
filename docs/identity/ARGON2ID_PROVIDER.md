# Aurora Identity Argon2id Provider

Status: **isolated implementation milestone**
Version: **0.1**

## Purpose

Aurora Identity uses Argon2id as the memory-hard verifier for Aurora Keys. This milestone implements the verifier provider behind the existing `aurora_identity_crypto_ops` contract without connecting it to the live Aurora OS login path.

The goals are:

- use a reviewed implementation rather than inventing a password-hashing primitive;
- preserve Aurora's opaque HMAC lookup-tag design;
- bound memory/CPU requests before executing KDF work;
- verify the implementation against RFC 9106;
- keep production entropy, key provisioning, service IPC and protected storage as separate gates.

## Upstream implementation

Aurora pins the official Password Hashing Competition Argon2 reference repository as a Git submodule:

```text
P-H-C/phc-winner-argon2
commit f57e61e19229e23c4445b85494dbf7c07de721cb
```

The upstream package permits use under CC0 1.0 or Apache License 2.0. Aurora keeps the upstream repository intact instead of copying and modifying the cryptographic source in-tree.

The gitlink, not a moving branch reference, is authoritative. Updating Argon2 therefore requires an explicit repository change and a new review/test cycle.

## Aurora provider contract

Aurora-owned code lives in:

```text
services/identity/include/aurora/identity/argon2id_provider.h
services/identity/src/argon2id_provider.c
```

The provider composes two independent operations:

```text
normalized Aurora Key
        |
        +--> HMAC-SHA256 lookup tag
        |       (protected persistent lookup key)
        |
        +--> Argon2id verifier
                (unique salt + versioned m/t/p)
```

The lookup tag is only an index. Authentication still requires successful Argon2id verification.

## Parameter version

Current Aurora KDF metadata uses:

```text
algorithm          = AURORA_IDENTITY_KDF_ARGON2ID
parameters_version = AURORA_IDENTITY_ARGON2ID_PARAMETERS_VERSION_1
Argon2 version     = 0x13 / decimal 19
```

Parameter version 1 means the stored `memory_kib`, `time_cost` and `parallelism` values are passed directly to Argon2id version 1.3 after policy validation.

Future parameter formats or migration rules must use a new Aurora parameter version rather than silently changing version-1 semantics.

## Bounded execution policy

Persisted KDF metadata is untrusted input. A malformed or corrupted identity record must not be able to request arbitrary resources.

The provider is initialized with explicit bounds for:

- minimum/maximum memory in KiB;
- minimum/maximum time cost;
- minimum/maximum parallelism;
- minimum/maximum salt size;
- minimum/maximum verifier size.

Before invoking Argon2, the provider also verifies Argon2's minimum memory-per-lane requirement.

Requests outside policy fail before memory-hard work begins.

The active Identity creation policy must itself fit inside the provider bounds. Production limits and normal creation parameters will be calibrated against supported Aurora hardware classes instead of being inferred from an untrusted database record.

## Verification path

Conceptually:

```text
candidate Aurora Key
    -> canonical normalization
    -> HMAC lookup tag
    -> load key record
    -> validate stored KDF metadata against provider limits
    -> Argon2id(candidate, stored salt, stored m/t/p)
    -> constant-time comparison with stored verifier
    -> authentication result
```

Temporary derived verifier bytes are cleared after comparison.

Raw Aurora Keys are never stored by this provider.

## Threading model

The host validation build compiles the upstream reference implementation with `ARGON2_NO_THREADS`.

This does **not** remove Argon2 parallelism from the algorithm: lanes and `parallelism` remain part of the hash definition and affect the result. It only makes the current host test execution path serial, avoiding a pthread dependency in the isolated Identity milestone.

A future Ring 3 production integration may choose a reviewed threaded execution strategy once Aurora's userspace threading/runtime policy is ready. Changing execution scheduling must not change verifier outputs.

## Test coverage

`test_identity_argon2id_provider.c` verifies:

- the official RFC 9106 Argon2id v=19 test vector, including secret and associated-data inputs at the underlying reference-library level;
- Aurora provider derivation matches the pinned reference implementation;
- correct Aurora Key verification succeeds;
- incorrect Aurora Key verification returns a non-match;
- full `aurora_identity_crypto_ops` wiring is present;
- unsupported Aurora parameter versions are rejected;
- excessive memory/time/parallelism values are rejected;
- insufficient memory for the requested lane count is rejected;
- invalid provider limits are rejected;
- provider state can be explicitly cleared.

The dedicated Identity CI checks out the pinned submodule recursively and builds Aurora-owned provider code under strict warning-as-error C11 flags.

## Production boundaries still open

This milestone completes the isolated Argon2id verifier implementation, but it does not make Aurora Identity production-authentication ready.

Still required:

1. reviewed Aurora entropy collection and DRBG seeding/reseeding;
2. protected provisioning and persistence of the Aurora Key lookup-HMAC key;
3. Argon2id parameter calibration for Aurora hardware classes;
4. protected service-owned system state on AuroraFS;
5. Aurora-native secure allocator/secret-memory policy for the Ring 3 Identity Service;
6. Ring 3 service lifecycle and capability-authorized IPC;
7. Session Manager integration;
8. end-to-end live login testing.

Until these gates are complete, the current framebuffer login remains a bootstrap/recovery prototype.
