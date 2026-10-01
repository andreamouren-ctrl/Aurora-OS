# Aurora OS Roadmap

Status: **Active implementation**
Version: **0.8**

The roadmap freezes architectural contracts before production hardening, while implementation prototypes are allowed to advance where the contracts are sufficiently clear.

## M0 — Definition and boot foundation

### M0.0 — Vision
- [x] Define project mission
- [x] Define efficiency-first philosophy
- [x] Define privacy-first philosophy
- [x] Define Desktop as primary customizable environment
- [x] Define Web Surfaces concept
- [x] Define Activity Spaces concept
- [x] Define Aurora Memory Fabric concept
- [x] Decide against a proprietary programming language

### M0.1 — Architecture contracts
- [x] Choose initial modular hybrid capability-kernel direction
- [x] Define kernel/user boundary: Ring 0 kernel / Ring 3 isolated processes
- [x] Define syscall philosophy: x86_64 SYSCALL/SYSRET with capability-aware dispatch
- [x] Define initial bounded IPC prototype model
- [x] Define initial typed capability prototype model
- [x] Define Android-style runtime permission philosophy
- [x] Define Aurora Identity Service / System App / fallback split
- [x] Define Aurora Identity authentication-factor policy
- [x] Define Aurora Identity logical IPC/capability contract
- [x] Define removable authenticator security tiers and revocation model
- [ ] Freeze permission broker contract
- [ ] Freeze permission manifest schema
- [ ] Freeze permission audit/history model
- [ ] Freeze driver model
- [x] Define user-memory copy boundary and supervisor hardening
- [ ] Freeze memory manager contracts
- [ ] Freeze scheduler classes
- [ ] Freeze boot information contract
- [ ] Freeze logging and panic contracts

### M0.2 — First verified boot

- [x] Native framebuffer boot UI with real subsystem progress
- [x] Integrated fullscreen Aurora boot artwork
- [x] Poetic staged boot messages
- [x] Visible framebuffer panic screen
- [x] Build freestanding x86_64 kernel
- [x] Produce bootable ISO
- [x] BIOS QEMU smoke boot reaches M1 user-space bootstrap
- [ ] Runtime-verify UEFI QEMU boot
- [x] Serial logging implementation and CI runtime verification
- [x] Framebuffer initialization exercised during CI boot
- [x] Panic/exception diagnostics implementation
- [ ] Produce raw USB image

## M1 — Kernel foundations

### Platform and execution
- [x] ACPI / MADT platform discovery
- [x] Local APIC + I/O APIC bootstrap
- [x] HPET / invariant-TSC monotonic clock bootstrap
- [x] x86_64 IDT and CPU exception handlers
- [x] tickless / one-shot timer bootstrap and runtime probe
- [x] SMP bring-up infrastructure to parked AP state
- [ ] Runtime-verify multi-vCPU SMP path in CI

### Memory
- [x] bootstrap physical memory manager
- [x] bootstrap virtual memory manager
- [x] bootstrap kernel heap
- [x] permission-checked usercopy layer

### Isolation and scheduling
- [x] BSP preemptive kernel-thread scheduler prototype
- [x] scheduler runtime self-test during boot
- [x] isolated Ring 3 process prototype with private CR3
- [x] per-thread kernel stack + TSS Ring 3 return path
- [x] Ring 3 fault-containment architecture
- [x] x86_64 SYSCALL/SYSRET entry + EXIT path
- [x] Ring 3 SYSCALL/EXIT runtime boot probe

### Security and IPC
- [x] x86_64 WP / SMEP / SMAP / UMIP hardening where supported
- [x] bounded IPC prototype with capability escrow
- [x] capability prototype with typed rights, revocation and delegation
- [x] capability self-test during boot
- [x] IPC capability-transfer self-test during boot

### Input and native session bootstrap
- [x] generic kernel input-event queue
- [x] IRQ-driven PS/2 keyboard prototype
- [x] boot-to-login framebuffer handoff
- [x] native Aurora Identity login UI prototype
- [x] interactive Aurora Key entry, masking, backspace, escape and submit states
- [ ] persistent Aurora Identity Service in user space
- [ ] secure credential verifier implementation
- [ ] persistent local identity database
- [ ] authenticated session bootstrap

## M2 — Aurora Memory Fabric prototype

- [ ] working-set instrumentation
- [ ] memory pressure states
- [ ] reclaimable cache classes
- [ ] inactive process detection
- [ ] process freezing
- [ ] compressed memory pool
- [ ] storage-backed paging
- [ ] memory telemetry
- [ ] pressure benchmarks

## M3 — Storage and system integrity

- [ ] VFS
- [ ] initial filesystem support
- [ ] persistent system/data storage services
- [ ] protected system-state location for Aurora Identity
- [ ] transactional identity database backend
- [ ] removable block-device discovery
- [ ] system/data separation
- [ ] package model
- [ ] signed package metadata
- [ ] transactional deployment prototype
- [ ] rollback
- [ ] live USB persistence model

## M4 — Graphics and Desktop foundation

- [ ] display subsystem beyond boot framebuffer
- [ ] compositor
- [ ] full input stack (including modern USB HID path)
- [ ] pointer/mouse stack
- [ ] window/surface protocol
- [ ] Desktop shell
- [ ] Activity Space prototype
- [ ] persistent activity state
- [ ] customization framework
- [ ] compositor-backed Aurora Identity pre-session surface
- [ ] Aurora Identity lock / re-authentication surface

## M5 — Network and Web Surfaces

- [ ] network stack integration
- [ ] DNS/TLS services
- [ ] Web Surface host API
- [ ] sandbox boundary
- [ ] per-surface capabilities
- [ ] privacy controls
- [ ] desktop-integrated navigation/search

## M6 — Native application platform

- [ ] application package format
- [ ] sandbox
- [ ] capability broker
- [ ] runtime permission prompts
- [ ] one-time / while-in-use / persistent grants
- [ ] scoped file and device grants
- [ ] central Privacy & Permissions UI
- [ ] sensitive-resource usage indicators
- [ ] permission history and revocation
- [ ] native UI toolkit
- [ ] application lifecycle
- [ ] freeze/resume integration
- [ ] notifications
- [ ] background execution policy
- [ ] privileged system-service lifecycle
- [ ] Aurora Identity Service production host
- [ ] Aurora Identity System App
- [ ] Session Manager

## M7 — Recovery, installation and updates

- [ ] live image
- [ ] persistent live mode
- [ ] installer
- [ ] atomic system update
- [ ] rollback UI
- [ ] recovery environment
- [ ] integrity verification
- [ ] Aurora Identity recovery credential
- [ ] framebuffer/recovery Identity path independent from compositor
- [ ] identity database integrity/repair flow

## M8 — Aurora Identity authenticators

### Aurora Identity Drive
- [ ] USB host/controller support required by target hardware
- [ ] USB mass-storage support
- [ ] removable-media broker
- [ ] versioned Identity Drive credential container
- [ ] machine-bound drive enrollment
- [ ] drive insertion/removal authentication flow
- [ ] optional Drive + PIN policy
- [ ] multiple enrolled drives
- [ ] independent revocation
- [ ] malformed-media fuzzing

### Secure authenticators
- [ ] USB HID/security-key transport or equivalent
- [ ] challenge-response authenticator API
- [ ] non-exportable hardware key support
- [ ] hardware authenticator enrollment/revocation

### Future federation
- [ ] trusted Aurora device enrollment
- [ ] encrypted identity metadata synchronization
- [ ] device revocation
- [ ] optional trusted-device recovery approval
- [ ] preserve fully offline local login

## Current development gate

Aurora can now boot through the M1 kernel foundation and enter the native Aurora Identity login prototype. The keyboard/input prototype is functional, but the current framebuffer path is not the final Identity architecture.

Canonical Aurora Identity architecture is now:

```text
Aurora Identity System App
        -> versioned capability-authorized IPC
        -> Aurora Identity Service
        -> one-time authenticated session grant
        -> Session Manager

Framebuffer Login
        -> retained as bootstrap/recovery fallback
```

The next persistent-login milestone must not place account policy or credential verification inside the kernel.

The preferred dependency order is:

1. establish minimal persistent storage/VFS foundations;
2. establish protected durable system state;
3. establish the first isolated user-space service execution model;
4. add secure randomness and an audited memory-hard credential verifier;
5. implement Aurora Identity Service and its versioned capability-authorized IPC contract;
6. persist identity records and reboot-resistant rate-limit state;
7. implement Session Manager and authenticated user profile bootstrap;
8. replace normal framebuffer login with the compositor-backed Aurora Identity System App while retaining fallback;
9. add removable USB storage infrastructure and Aurora Identity Drive;
10. add recovery and secure-hardware authenticators.

Detailed Identity implementation sequencing is defined in `docs/identity/IMPLEMENTATION_ROADMAP.md`.

The canonical Aurora Identity specification set is indexed in `docs/identity/README.md` and governed by `docs/AURORA_IDENTITY.md`.

## Cross-cutting requirement

Every milestone must be evaluated against:

- performance;
- memory efficiency;
- latency;
- privacy;
- security;
- recoverability;
- measurable resource cost.
