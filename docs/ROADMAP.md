# Aurora OS Roadmap

Status: **Initial planning**
Version: **0.6**

The roadmap deliberately freezes architecture before expanding implementation.

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
- [ ] Choose kernel architecture model
- [x] Define kernel/user boundary: Ring 0 kernel / Ring 3 isolated processes
- [x] Define syscall philosophy: x86_64 SYSCALL/SYSRET with capability-aware dispatch
- [ ] Define IPC model
- [ ] Define capability model
- [x] Define Android-style runtime permission philosophy
- [ ] Define permission broker contract
- [ ] Define permission manifest schema
- [ ] Define permission audit/history model
- [ ] Define driver model
- [x] Define user-memory copy boundary and supervisor hardening
- [ ] Define memory manager contracts
- [ ] Define scheduler classes
- [ ] Define boot information contract
- [ ] Define logging and panic contracts

### M0.2 — First verified boot

- [x] Native framebuffer boot UI with real subsystem progress
- [x] Poetic staged boot messages
- [x] Visible framebuffer panic screen
- [x] Build freestanding x86_64 kernel (compile/link verified)
- [ ] Boot under UEFI in QEMU
- [ ] Boot under BIOS where supported
- [x] Serial logging implementation (runtime verification pending)
- [x] Framebuffer initialization implementation (runtime verification pending)
- [x] Panic/exception diagnostics implementation (runtime verification pending)
- [ ] Produce bootable ISO
- [ ] Produce raw USB image

## M1 — Kernel foundations

- [x] ACPI / MADT platform discovery (runtime verification pending)
- [x] Local APIC + I/O APIC bootstrap (runtime verification pending)
- [x] HPET / invariant-TSC monotonic clock bootstrap (runtime verification pending)

- [x] bootstrap physical memory manager
- [x] bootstrap virtual memory manager
- [x] bootstrap kernel heap
- [x] x86_64 IDT and CPU exception handlers
- [x] tickless timer bootstrap (runtime verification pending)
- [x] SMP bring-up to parked AP state (runtime verification pending)
- [x] BSP preemptive kernel-thread scheduler prototype (runtime verification pending)
- [x] x86_64 SYSCALL/SYSRET entry + EXIT path (runtime verification pending)
- [x] isolated Ring 3 process prototype with private CR3 (runtime verification pending)
- [x] per-thread kernel stack + TSS Ring 3 return path (runtime verification pending)
- [x] Ring 3 fault containment without kernel panic (runtime verification pending)
- [x] permission-checked usercopy layer (runtime verification pending)
- [x] x86_64 WP / SMEP / SMAP / UMIP hardening where supported
- [x] bounded IPC prototype with capability escrow (runtime verification pending)
- [x] capability prototype with typed rights, revocation and delegation (runtime verification pending)

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
- [ ] system/data separation
- [ ] package model
- [ ] signed package metadata
- [ ] transactional deployment prototype
- [ ] rollback
- [ ] live USB persistence model

## M4 — Graphics and Desktop foundation

- [ ] display subsystem
- [ ] compositor
- [ ] input stack
- [ ] window/surface protocol
- [ ] Desktop shell
- [ ] Activity Space prototype
- [ ] persistent activity state
- [ ] customization framework

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

## M7 — Recovery, installation and updates

- [ ] live image
- [ ] persistent live mode
- [ ] installer
- [ ] atomic system update
- [ ] rollback UI
- [ ] recovery environment
- [ ] integrity verification

## Cross-cutting requirement

Every milestone must be evaluated against:

- performance;
- memory efficiency;
- latency;
- privacy;
- security;
- recoverability;
- measurable resource cost.
