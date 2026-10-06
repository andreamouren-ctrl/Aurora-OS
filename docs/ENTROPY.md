# Aurora OS Entropy Seed Foundation

Status: **kernel seed foundation + capability-gated Ring 3 handoff; production Identity DRBG integration still pending**
Version: **0.2**

## 1. Purpose

Aurora OS needs unpredictable seed material for security-sensitive subsystems such as Aurora Identity. This document defines the kernel seed-source foundation and the controlled Ring 3 handoff used by trusted services.

The subsystem deliberately exposes **seed material**, not a general-purpose random-number API.

```text
qualified hardware/platform seed source
        |
        v
Aurora kernel entropy policy
        |
        v
entropy_fill_seed()
        |
        +--> kernel consumers
        |
        +--> AURORA_CAP_ENTROPY + AURORA_SYS_ENTROPY_SEED
                 |
                 v
          trusted Ring 3 service
                 |
                 v
          consumer-owned reviewed DRBG
```

Aurora Identity must use this seed material to instantiate or reseed its HMAC-DRBG. The kernel seed service does not replace the DRBG.

## 2. Current x86_64 sources

### RDSEED

`RDSEED` is the only source currently allowed to make the service `ready`.

Aurora:

- detects support with CPUID leaf 7, EBX bit 18;
- executes `RDSEED` only after capability detection;
- uses bounded instruction retries;
- performs startup and continuous health checks before releasing seed bytes.

This is intentionally conservative. Future Aurora releases may combine multiple independently reviewed sources, but this milestone does not pretend those sources already exist.

### RDRAND

Aurora detects `RDRAND` with CPUID leaf 1, ECX bit 30, but treats it as **auxiliary-only**.

A machine that exposes RDRAND but not RDSEED therefore remains:

```text
entropy initialized = yes
trusted seed ready   = no
```

RDRAND availability is reported for diagnostics and future conditioning work, but RDRAND alone does not authorize trusted seed output.

## 3. Sources not counted as trusted entropy

The current policy does not count any of the following as trusted entropy merely because they vary:

- TSC values;
- monotonic clock values;
- timing jitter;
- device serial numbers;
- memory addresses;
- boot timestamps;
- process/thread identifiers;
- filesystem state.

Such inputs may become additional material in a future reviewed conditioner, but they must not independently make the service ready.

## 4. Health policy

### Startup test

Before the service becomes ready, it requires `AURORA_ENTROPY_STARTUP_SAMPLES` accepted source words. Version 0.2 requires eight.

A sample is rejected if it is:

- all zero;
- all one bits;
- identical to the immediately previous accepted sample.

Any health-test failure during startup prevents trusted output.

### Continuous test

The same checks continue across successful output requests.

If a runtime health failure occurs:

- the current request fails;
- the complete caller output buffer is cleared;
- `health_failed` is latched;
- the service stops reporting `ready`.

A transient source read failure is recorded separately. It fails that request and clears its output, but it is not automatically reclassified as a deterministic health failure.

## 5. Fail-closed behavior

`entropy_fill_seed()` succeeds only while the trusted seed service is ready.

On a physical machine or VM without a qualified source, Aurora OS still boots. Security-sensitive consumers must remain unavailable rather than silently falling back to weak pseudo-random material.

```text
OS bootable                 = yes
secure seed currently ready = maybe
production Identity allowed = only after all Identity gates are satisfied
```

Absence of RDSEED is therefore not a kernel panic condition.

## 6. Kernel API

Primary kernel interface:

```c
bool entropy_init(void);
bool entropy_ready(void);
bool entropy_fill_seed(void *buffer, size_t size);
struct aurora_entropy_status entropy_get_status(void);
```

`entropy_fill_seed()` is bounded by `AURORA_ENTROPY_MAX_SEED_REQUEST` and is intended for DRBG seed/reseed operations, not bulk random-data generation.

The source abstraction in `entropy_source.h` separates policy from architecture-specific collection. This permits host testing and future non-x86 platform backends without changing the public seed-service contract.

## 7. Capability-gated Ring 3 handoff

Trusted Ring 3 services can receive a dedicated capability of type:

```text
AURORA_CAP_ENTROPY
```

The current trusted-service bootstrap grants this capability only when the service manifest explicitly requests it. The grant carries only:

```text
AURORA_RIGHT_READ
```

It does **not** carry `WRITE`, `CONTROL`, or `TRANSFER` authority.

The Ring 3 syscall is:

```text
AURORA_SYS_ENTROPY_SEED = 9
```

A request supplies:

- the process-local entropy capability handle;
- a user-space destination buffer;
- a requested seed length.

The current Ring 3 request limit is 64 bytes per syscall. The kernel:

1. validates the capability type and `READ` right;
2. validates the bounded request length;
3. obtains seed material through `entropy_fill_seed()`;
4. copies the seed to checked user memory only after successful collection;
5. securely clears the temporary kernel seed buffer on every exit path.

The syscall never exposes RDSEED/RDRAND instructions or raw entropy-source control to Ring 3.

## 8. Trusted-service startup ABI

Trusted-service startup ABI version 2 carries three process-local handles:

```text
IPC endpoint
Protected System State
Entropy seed capability (optional)
```

Aurora Identity requests the entropy capability explicitly. On a supervised service restart the previous process and capability table are destroyed; the new generation receives a freshly granted process-local entropy handle rather than inheriting stale authority.

This is an authority transport only. The current runtime probe does not yet instantiate the production `services/identity` HMAC-DRBG.

## 9. Boot integration

The x86_64 backend initializes the seed service during early architecture setup, before ordinary user-space services exist.

The serial log reports source availability and readiness but never logs seed bytes.

When trusted entropy is available, the runtime Ring 3 proof emits:

```text
[ring3-entropy] capability-gated trusted seed syscall probe passed
```

When it is unavailable, Aurora logs that the capability probe was skipped and continues booting. Production Identity must remain fail-closed in that state.

## 10. Testing

Host-side policy tests use deterministic simulated sources and cover:

- healthy trusted-source startup;
- RDRAND-only/untrusted startup;
- successful seed extraction;
- duplicate startup sample rejection;
- continuous duplicate detection;
- output-buffer clearing on failure;
- transient read failure accounting;
- invalid request rejection.

The Ring 3 runtime test additionally proves:

- seed output requires `AURORA_CAP_ENTROPY` with `READ`;
- the capability lacks `WRITE` and `TRANSFER` rights;
- a user process receives non-empty seed material through checked usercopy;
- the process/thread/address-space resources are reclaimed after the probe;
- trusted Identity bootstrap and supervisor generations receive fresh read-only entropy authority.

A dedicated CI boot uses a QEMU CPU model exposing the trusted source and requires the Ring 3 success marker. The normal boot matrix still verifies that Aurora remains bootable when entropy is not available.

## 11. Current limitations

Version 0.2 intentionally does not yet provide:

- a cryptographic multi-source conditioner/pool;
- entropy estimation from interrupt/timing sources;
- TPM or firmware RNG integration;
- complete VM/hypervisor trust policy for virtualized hardware RNG instructions;
- runtime source registration;
- automatic long-lived-service DRBG reseed scheduling;
- per-consumer DRBG instances provisioned by the real services;
- hardware-backed sealing of Identity machine secrets;
- production certification of Aurora Identity login.

The capability-gated seed handoff exists, but Aurora Identity is not production-ready until the real service consumes it and implements the DRBG instantiate/reseed/failure lifecycle.

## 12. Next milestones

For Aurora Identity, the immediate sequence is now:

1. bind the real `services/identity` runtime to the startup entropy capability;
2. instantiate its HMAC-DRBG from trusted kernel seed material and define reseed/failure policy;
3. finish the Aurora-native Protected State backend for the Identity database;
4. load/provision the Machine Identity root secret from the real service;
5. replace the runtime probe with the real Identity executable and route authentication IPC through it;
6. add further independently reviewed entropy sources/conditioning where justified.

Until the real Identity consumer lifecycle is complete, this component remains the **Aurora kernel entropy seed foundation with controlled Ring 3 handoff**, not a complete production random subsystem.
