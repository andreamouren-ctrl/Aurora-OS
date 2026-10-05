# Aurora OS Entropy Seed Foundation

Status: **kernel foundation, not yet a complete production entropy service**
Version: **0.1**

## 1. Purpose

Aurora OS needs unpredictable seed material for security-sensitive subsystems such as Aurora Identity. This document defines the first kernel-level seed-source foundation.

The current service deliberately exposes **seed material**, not a general-purpose random-number API.

```text
qualified hardware/platform seed source
        |
        v
Aurora kernel entropy policy
        |
        v
entropy_fill_seed()
        |
        v
consumer-owned reviewed DRBG
```

Aurora Identity is expected to use this seed material to instantiate or reseed its HMAC-DRBG. The kernel seed service does not replace the DRBG.

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

Aurora detects `RDRAND` with CPUID leaf 1, ECX bit 30, but treats it as **auxiliary-only** in this milestone.

A machine that exposes RDRAND but not RDSEED therefore remains:

```text
entropy initialized = yes
trusted seed ready   = no
```

RDRAND availability is reported for diagnostics and future conditioning work, but RDRAND alone does not authorize `entropy_fill_seed()` output today.

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

The seed service uses two conservative checks around the trusted 64-bit source.

### Startup test

Before the service becomes ready, it requires `AURORA_ENTROPY_STARTUP_SAMPLES` accepted source words. Version 0.1 requires eight.

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

On an ordinary machine or virtual machine without a qualified source, Aurora OS still boots. Security-sensitive consumers must remain unavailable rather than silently falling back to weak pseudo-random material.

This gives Aurora the explicit state:

```text
OS bootable                 = yes
secure seed currently ready = maybe
production Identity allowed = only after all Identity gates are satisfied
```

Absence of RDSEED is therefore not a kernel panic condition.

## 6. API

Primary interface:

```c
bool entropy_init(void);
bool entropy_ready(void);
bool entropy_fill_seed(void *buffer, size_t size);
struct aurora_entropy_status entropy_get_status(void);
```

`entropy_fill_seed()` is bounded by `AURORA_ENTROPY_MAX_SEED_REQUEST` and is intended for DRBG seed/reseed operations, not bulk random-data generation.

The source abstraction in `entropy_source.h` separates policy from architecture-specific collection. This permits host testing and future non-x86 platform backends without changing the public seed-service contract.

## 7. Boot integration

The x86_64 backend initializes the seed service during early architecture setup, before ordinary user-space services exist.

The serial log reports:

- RDSEED availability;
- RDRAND availability;
- whether the trusted seed service is currently ready.

No raw seed values are logged.

## 8. Testing

Host-side policy tests use deterministic simulated sources and cover:

- healthy trusted-source startup;
- RDRAND-only/untrusted startup;
- successful seed extraction;
- duplicate startup sample rejection;
- continuous duplicate detection;
- output-buffer clearing on failure;
- transient read failure accounting;
- invalid request rejection.

The normal Aurora OS build additionally compiles the real freestanding x86_64 RDSEED/RDRAND backend and exercises the normal boot path in QEMU.

Tests never claim deterministic fixture values are real entropy.

## 9. Current limitations

Version 0.1 intentionally does not yet provide:

- a cryptographic multi-source conditioner/pool;
- entropy estimation from interrupt/timing sources;
- TPM or firmware RNG integration;
- VM/hypervisor trust policy for virtualized hardware RNG instructions;
- runtime source registration;
- automatic DRBG reseed scheduling;
- a Ring 3 entropy broker or capability-authorized syscall/IPC API;
- per-consumer DRBG instances;
- persistent machine-secret provisioning;
- production certification of Aurora Identity login.

## 10. Next milestones

The next security milestones are:

1. define protected machine/service-secret provisioning;
2. provision the persistent Aurora Identity lookup-HMAC key without logging or exposing it to ordinary applications;
3. define the capability-authorized Ring 3 seed handoff for privileged services;
4. connect Aurora Identity HMAC-DRBG instantiation/reseed to the kernel seed service;
5. add additional independently reviewed entropy sources and a cryptographic conditioner where justified;
6. define reseed/failure/recovery policy for long-lived services.

Until those gates are complete, this component should be described as the **Aurora kernel entropy seed foundation**, not as a complete production random subsystem.
