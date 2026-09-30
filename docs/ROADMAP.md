# Aurora OS Roadmap

Status: **Initial planning**
Version: **0.2**

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
- [ ] Define kernel/user boundary
- [ ] Define syscall philosophy
- [ ] Define IPC model
- [ ] Define capability model
- [x] Define Android-style runtime permission philosophy
- [ ] Define permission broker contract
- [ ] Define permission manifest schema
- [ ] Define permission audit/history model
- [ ] Define driver model
- [ ] Define memory manager contracts
- [ ] Define scheduler classes
- [ ] Define boot information contract
- [ ] Define logging and panic contracts

### M0.2 — First verified boot
- [ ] Build freestanding x86_64 kernel
- [ ] Boot under UEFI in QEMU
- [ ] Boot under BIOS where supported
- [ ] Serial logging
- [ ] Framebuffer initialization
- [ ] Panic screen
- [ ] Produce bootable ISO
- [ ] Produce raw USB image

## M1 — Kernel foundations

- [ ] physical memory manager
- [ ] virtual memory manager
- [ ] kernel heap
- [ ] interrupts/exceptions
- [ ] timer
- [ ] SMP bring-up
- [ ] scheduler prototype
- [ ] syscall entry
- [ ] userspace process prototype
- [ ] IPC prototype
- [ ] capability prototype

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
