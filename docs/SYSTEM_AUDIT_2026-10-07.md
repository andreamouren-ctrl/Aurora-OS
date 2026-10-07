# Aurora OS Full System Audit and Competitive Position

Status: **Canonical status audit**
Audit date: **2026-10-07**
Repository baseline: `main` at `f928b52414ffd7af4f61134ad9a8ffc85f6410c9`

This document records the implemented and runtime-verified state of Aurora OS and compares the current architecture with a contemporary Ubuntu/Linux and Windows baseline.

It is a **status document**, not a product-readiness claim. A clean architectural contract is not equivalent to the decades of field validation, hardware coverage, compatibility, performance tuning and security hardening present in Linux or Windows.

## 1. Comparison baseline

The external comparison baseline used for this audit is:

- Ubuntu 26.04 LTS / Linux kernel 7.0 class systems;
- contemporary Windows 11 / Windows NT with the WDDM 3.x graphics model.

Reference documentation:

- Linux scheduler: https://docs.kernel.org/scheduler/
- Linux memory management: https://docs.kernel.org/mm/
- Ubuntu 26.04 release notes: https://documentation.ubuntu.com/release-notes/26.04/
- Windows VBS/HVCI: https://learn.microsoft.com/windows-hardware/design/device-experiences/oem-vbs
- Windows WDDM overview: https://learn.microsoft.com/windows-hardware/drivers/display/windows-vista-display-driver-model-design-guide

Comparison labels:

- **AHEAD BY DESIGN** — Aurora currently has a narrower or cleaner authority/contract model in this area; this does not imply greater production maturity.
- **COMPARABLE MECHANISM** — Aurora implements the same class of operating-system mechanism, but generally with less breadth, tuning and hardware validation.
- **BEHIND** — Aurora lacks major functionality, maturity, optimization or hardware coverage present in Linux/Windows.
- **ABSENT / FUTURE** — the subsystem does not yet exist as a production-capable Aurora implementation.

## 2. Executive result

Aurora OS is no longer accurately described as a boot demo or a collection of disconnected kernel experiments.

The verified vertical path now includes:

```text
boot
 -> x86_64 platform/interrupts
 -> SMP CPU bring-up
 -> topology-aware preemptive scheduler
 -> Ring 3 process isolation
 -> SYSCALL/SYSRET
 -> capability enforcement
 -> bounded/blocking IPC
 -> process/thread reclamation
 -> trusted Ring 3 service supervision
 -> ATA/AHCI/NVMe block transports
 -> MBR/GPT
 -> VFS/mount framework
 -> AuroraFS v2
 -> protected system state
 -> persistent Ring 3 Identity Service
 -> Session Manager
 -> persistent user profile authority
 -> ordinary Ring 3 User Session Host
 -> Display/Surface/Compositor foundations
 -> normalized keyboard/pointer input work
```

Aurora remains far behind Linux and Windows as a complete consumer OS because networking, USB, audio, power management, broad GPU acceleration, application ecosystem and real-hardware driver coverage are not yet comparable.

## 3. Kernel/platform inventory

### Boot and architecture

Implemented:

- Limine-based x86_64 boot;
- BIOS boot verified in QEMU;
- framebuffer boot/recovery path;
- GDT/TSS, IDT and exception handling;
- ACPI/MADT discovery;
- Local APIC and I/O APIC;
- HPET/monotonic clock;
- Local APIC one-shot timers;
- CR0.WP plus SMEP/SMAP/UMIP where supported.

Position:

- **COMPARABLE MECHANISM** for the basic privileged execution model.
- **BEHIND** Linux/Windows for firmware breadth, secure/measured boot, suspend/resume, power/thermal policy and real-hardware validation.

### SMP and CPU topology

Implemented and runtime exercised:

- application-processor bring-up;
- CPU-local execution state;
- per-CPU scheduler ownership;
- real AP scheduler-stack handoff;
- per-CPU Local APIC timer operation;
- physical topology discovery (package/core/SMT);
- core-first / SMT-second soft placement;
- synchronous cross-CPU TLB shootdown with IPI acknowledgement.

Position:

- **COMPARABLE MECHANISM** for core SMP primitives.
- **BEHIND** Linux/Windows for NUMA, heterogeneous CPU capacity/energy policy, scheduler classes, large-machine scaling and decades of race/errata coverage.

### Scheduler

Implemented:

- preemptive kernel scheduling;
- multi-CPU scheduling;
- per-CPU current/idle state;
- topology-aware soft placement;
- work stealing fallback;
- BLOCKED/RUNNABLE wakeup path for Ring 3 IPC;
- deterministic scheduler/runtime probes.

Position:

- **BEHIND** Linux EEVDF/realtime/deadline/sched_ext and the Windows production scheduler in breadth and tuning.
- The current scheduler architecture is sound enough to extend without a known fundamental redesign requirement.

## 4. Memory management

Implemented:

- physical page allocator;
- virtual address spaces/private CR3;
- kernel heap with reusable free ranges;
- checked usercopy;
- anonymous Ring 3 memory;
- refcounted shared-memory objects;
- process-owned mapping cleanup;
- address-space destruction;
- TLB shootdown;
- secret-aware heap/object scrubbing on lifecycle paths.

Position:

- **COMPARABLE MECHANISM** for basic virtual-memory isolation and cross-CPU translation coherence.
- **BEHIND** Linux/Windows for demand paging, page cache integration, swap/pagefile, general file mappings, copy-on-write process semantics, NUMA, huge-page policy, reclaim sophistication, working-set accounting and OOM policy.

## 5. Process and thread lifecycle

Runtime verified:

- isolated Ring 3 process creation;
- private address spaces;
- per-thread kernel stacks;
- process exit/fault results;
- scheduler thread termination and reaping;
- process/address-space reclamation;
- repeated create -> run -> exit -> reap -> reuse cycles;
- service instance recreation after complete teardown.

Current structural limit:

- the ordinary Ring 3 process model remains effectively one primary thread per process; full multi-thread process lifecycle semantics are future work.

Position:

- **COMPARABLE MECHANISM** for the current one-thread process lifecycle.
- **BEHIND** Linux/Windows for mature multi-threading, job/process groups, signals/events, debugging, accounting, resource controls and compatibility.

## 6. Syscall and IPC boundary

Implemented:

- native x86_64 SYSCALL/SYSRET;
- checked user-return state;
- capability-checked operations;
- bounded IPC send/receive;
- explicit capability transfer;
- Ring 3 blocking wait;
- lost-wakeup protection;
- shared-memory foundation;
- purpose-specific protected-state, profile and graphics/display operations.

Position:

- **AHEAD BY DESIGN** in several narrow privileged APIs because Aurora deliberately exposes less generic authority.
- **BEHIND** Linux/Windows in syscall/API breadth, asynchronous I/O breadth, synchronization primitives and compatibility.

## 7. Capability security model

Implemented:

- typed, unforgeable kernel capability handles;
- explicit rights;
- rights attenuation on delegation;
- controlled transfer;
- revocation;
- service dependency capabilities;
- profile/session capability leasing;
- graphics/display capability boundaries;
- protected-state authority separated from generic pathname access.

Position:

- **AHEAD BY DESIGN** for consistency of least-authority delegation in the currently implemented Aurora subsystems.
- This is not a claim that Aurora is more secure overall than Linux or Windows. Linux has mature DAC/LSM/AppArmor/seccomp/namespaces/cgroups and Windows has mature tokens/SIDs/ACLs plus VBS/HVCI and hardware-backed isolation.

## 8. Protected System State

Implemented:

- dedicated service-owned durable namespace;
- capability-gated Ring 3 record bridge;
- bounded record names and sizes;
- create-once semantics;
- fail-closed tri-state lookup semantics;
- no generic Ring 3 remove/rename/truncate namespace authority through this bridge.

Position:

- **AHEAD BY DESIGN** as a deliberately narrow trusted-service API.
- **BEHIND** mature Linux/Windows secure-storage ecosystems for hardware sealing, TPM integration, policy breadth and field validation.

## 9. Identity and session architecture

Implemented in the live OS:

- Ring 3 Aurora Identity Service;
- persistent identity/credential store;
- Aurora Key normalization;
- Argon2id provider;
- protected opaque lookup tags;
- machine-root-secret provisioning foundation;
- reboot-safe throttling state;
- first-user bootstrap creation policy;
- one-time session grants;
- capability-gated grant consumption;
- separate Ring 3 Session Manager;
- stable user_id session binding;
- persistent profile root on AuroraFS;
- reduced user/profile capability delegation;
- ordinary Ring 3 User Session Host;
- logout with revocation;
- lock/unlock with fresh same-user authentication;
- fail-closed abnormal session termination;
- purpose-bound re-authentication proof core.

Position:

- **AHEAD BY DESIGN** in the clarity of the credential -> one-time grant -> Session Manager -> profile capability chain and in avoiding credential exposure to the desktop/session host.
- **BEHIND** Windows and mature Linux stacks in authenticator breadth, enterprise policy, recovery, hardware-backed credentials, auditing, federation and years of security review.

## 10. Service lifecycle

Implemented:

- trusted Ring 3 service bootstrap from a kernel-owned manifest;
- explicit dependency-capability injection;
- blocking IPC service loop support;
- bounded restart supervision;
- complete process/thread reclamation before reconstruction;
- fresh capability tables and fresh service instance identity after restart.

Position:

- **AHEAD BY DESIGN** for explicit least-authority dependency construction.
- **BEHIND** systemd/Windows SCM for dependency graphs, health/watchdog policy, rate-limited restarts, discovery, configuration and ecosystem integration.

## 11. Storage transports

### ATA PIO

Implemented as a compatibility read/write/flush path.

Position: **BEHIND** modern Linux/Windows storage paths; retained primarily as compatibility and CI coverage.

### AHCI/SATA

Runtime verified:

- PCI/ABAR discovery;
- controller enable;
- SATA-port enumeration;
- IDENTIFY;
- DMA read/write;
- flush;
- block-device registration;
- end-to-end filesystem/VFS traversal.

Remaining:

- broader PRDT/batching;
- interrupt-driven completion;
- NCQ;
- hotplug;
- stronger timeout/reset/error recovery.

Position: **COMPARABLE MECHANISM** baseline, **BEHIND** production drivers.

### NVMe

Runtime verified:

- PCI/BAR/MMIO;
- controller enable;
- Admin queues;
- Identify Controller/Namespace;
- I/O queues;
- NVM read/write/flush;
- block-device registration;
- end-to-end filesystem/VFS traversal.

Remaining:

- MSI-X/per-CPU queue scaling;
- advanced recovery;
- multiple controller/namespace policy;
- hotplug/power-management hardening.

Position: **COMPARABLE MECHANISM** baseline, **BEHIND** production Linux/Windows NVMe stacks.

## 12. Partition and VFS layer

Implemented:

- MBR;
- GPT;
- GPT CRC validation;
- primary/backup GPT fallback;
- variable logical-block sizes including 4Kn validation paths;
- filesystem-driver registry;
- mount manager;
- VFS routing;
- fail-closed lookup distinction where required by trusted state.

Position:

- **COMPARABLE MECHANISM** for core partition/mount abstractions.
- **BEHIND** Linux/Windows for namespace features, cache integration, filters, async I/O and filesystem breadth.

## 13. AuroraFS v2

Runtime-verified/currently implemented foundations include:

- 4 KiB production-layout foundation;
- scalable bitmap allocation;
- nested directories;
- inline extents and bounded extent-tree levels 1/2/3;
- COW structural growth and durable inode publication;
- general create/mkdir;
- file write/truncate;
- remove/rename;
- free-space reclaim;
- crash-consistent namespace transactions;
- recovery/corruption gates;
- ownership/mode/timestamps;
- ACLs;
- explicit fsync/fdatasync/sync durability;
- real mount at `/system`.

The bounded Level-3 hierarchy is closed as a milestone; Level 4 is intentionally outside the current on-disk contract.

Position:

- **AHEAD BY DESIGN / COMPARABLE MECHANISM** for several modern integrity, COW and fail-closed design choices.
- **BEHIND** ext4/XFS/Btrfs/NTFS/ReFS for performance history, tooling, repair, quotas, snapshots/reflinks breadth, sparse-file maturity, encryption/compression integration, huge concurrency and real-world failure exposure.

AuroraFS should be described as a credible modern experimental filesystem, not as superior to production filesystems.

## 14. Foreign filesystems

Implemented:

- FAT32/VFAT read-only;
- Unicode long filenames;
- exFAT read-only;
- 512/1024/2048/4096 logical-block hardening where supported by each driver.

Position: **BEHIND** Linux/Windows due to read-only scope and limited filesystem catalogue.

## 15. Graphics/display

### G1 Display foundation

Complete/runtime verified:

- display output/mode objects;
- boot-framebuffer backend;
- compositor-owned backbuffer;
- bounded present;
- presentation serials;
- Ring 3 display capability path.

### G2 Surface/buffer core

Complete/runtime verified:

- shared-memory graphics buffers;
- capability-backed surfaces;
- attach/damage/commit;
- safe buffer/surface lifetime and recycling;
- frame callbacks;
- Ring 3 map/unmap;
- two-client cross-capability isolation.

### G3 Software compositor and color

Complete/runtime verified:

- bounded scene graph;
- deterministic z-order;
- clipping;
- alpha composition;
- damage aggregation/presentation;
- transforms and integer scaling;
- occlusion culling;
- secure-scene exclusion;
- RGB10A2, RGB12 and RGBA16F paths;
- direct 65,536-entry ST.2084 path;
- BT.2100 HLG;
- ICC v2/v4 matrix-shaper profiles;
- VCGT calibration and bounded 17^3 LUT;
- perceptual HDR tone mapping/gamut handling.

Display/link foundations also include DDC/EDID/CTA/DisplayID parsing, DP/HDMI capability models, VRR/DSC contracts, link-training contracts and a QEMU Standard VGA/Bochs VBE driver foundation.

Remaining major gaps:

- vendor GPU acceleration;
- GPU virtual memory/scheduling;
- native vendor scanout/color programming;
- ICC LUT A2B/B2A import;
- production multi-monitor/hotplug stack;
- 3D/Vulkan/OpenGL/Direct3D-class application API.

Position:

- **AHEAD BY DESIGN** for capability-scoped surfaces and cross-client authority boundaries.
- **COMPARABLE MECHANISM** for software composition/color fundamentals.
- **BEHIND** Linux DRM/KMS/Mesa/Wayland ecosystems and Windows WDDM/DWM/DirectX by a very large margin in acceleration, GPU scheduling, drivers and ecosystem.

## 16. Input

Implemented/current work:

- normalized device-independent input events;
- PS/2 keyboard;
- PS/2 mouse IRQ12 packet path;
- graphics input-routing foundation;
- explicit separation of pointer events from Aurora Key handling.

Still required:

- full compositor hit testing/focus/capture completion;
- USB HID;
- touch/multitouch;
- pen/gamepad;
- layout/IME/accessibility stack.

Position: **BEHIND** Linux/Windows.

## 17. Major absent/future subsystems

The largest functional gaps relative to Ubuntu/Linux and Windows are:

- production networking stack and DNS/TLS integration;
- USB/xHCI and broad USB class support;
- audio stack;
- mature power management/suspend/resume/thermal/battery stack;
- broad vendor GPU acceleration;
- mature desktop/window shell;
- package/application ecosystem;
- application sandbox/broker UX;
- printing;
- Bluetooth/Wi-Fi driver ecosystem;
- virtualization comparable with KVM/Hyper-V;
- broad debugging/crash-dump/telemetry infrastructure;
- real-hardware certification matrix.

These gaps dominate the current difference between Aurora and a daily-driver operating system.

## 18. Documentation audit findings

The 2026-10-07 audit found material stale statements in canonical documentation:

1. root `README.md` still described SMP as single-vCPU;
2. `docs/KERNEL_MODEL.md` still described APs as parked and the scheduler as BSP-only;
3. root `README.md` described AHCI as discovery-only and NVMe as unimplemented;
4. root and Identity documentation described persistent authentication / the live Identity Service as not yet integrated;
5. `docs/ROADMAP.md` retained old unchecked AuroraFS Level-3, mutation, recovery, metadata and Identity gates that are already closed;
6. the global M4 roadmap still described display/compositor/pointer work as absent despite G1-G3 completion and active G4 work;
7. `services/identity/README.md` still described the implementation as isolated from the live login/session path;
8. `docs/SERVICE_SUPERVISION.md` described the Identity production service as future work although a long-lived Ring 3 service/session chain now exists;
9. `docs/graphics/ARCHITECTURE.md` listed HDR/color management as out of scope even though the software mastering path is implemented and runtime verified.

The audit branch updates these documents and adds this file as the canonical cross-subsystem snapshot.

## 19. Current engineering judgement

Aurora is best described as a **structured experimental operating system with real kernel, storage, identity/session and graphics foundations**.

It is not yet competitive with Linux or Windows as a general-purpose desktop product.

The strongest differentiators today are:

- capability-first authority;
- explicit rights attenuation and revocation;
- credential-to-session separation;
- narrow Protected System State APIs;
- fail-closed security/storage behavior;
- service dependency capabilities;
- runtime acceptance gates for major milestones;
- a modern native filesystem developed together with the security/session model.

The highest-priority maturity gaps are:

1. finish G4 input focus/capture and USB HID;
2. implement G5 window protocol/Desktop Shell;
3. vendor GPU acceleration and production display programming;
4. networking;
5. USB stack and removable media;
6. advanced memory management;
7. power management;
8. real-hardware qualification and fault-injection/fuzzing expansion.

