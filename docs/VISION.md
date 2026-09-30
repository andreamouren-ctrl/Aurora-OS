# Aurora OS Vision

Status: **Canonical draft**
Version: **0.1**

## Mission

Aurora OS is a proprietary personal operating system designed around a simple principle:

> The computer should spend its resources on the user's current intent, not on invisible background work.

The system prioritizes speed, efficiency, privacy, low latency, predictability, recoverability, and user control.

Aurora OS must be recognizably different from existing desktop operating systems at the architectural level, not merely through visual design.

---

## 1. Efficiency first

Every permanent system component must justify its CPU time, memory footprint, storage I/O, wakeups, and energy cost.

Core rules:

- event-driven behavior is preferred over polling;
- inactive services should not remain busy;
- background work must yield to interactive work;
- unnecessary duplication of state should be avoided;
- zero-copy and shared-buffer designs should be preferred when safe;
- expensive work should be asynchronous where possible;
- idle applications should approach near-zero CPU use;
- boot and resume paths must be designed as performance-critical paths.

Performance is not an optional optimization phase. It is an architectural requirement.

---

## 2. Aurora Memory Fabric

Memory is treated as an adaptive hierarchy rather than a simple split between used RAM, free RAM, and swap.

The memory manager is expected to distinguish states such as:

- **Active**: latency-sensitive pages in current use;
- **Warm**: recently used data likely to be reused;
- **Cold**: low-priority data eligible for compression or eviction;
- **Compressed**: inactive pages retained in RAM in compressed form;
- **Frozen**: application state belonging to suspended workloads;
- **Disposable**: cache that can be reclaimed immediately.

The objective is twofold:

1. unused RAM should be exploited intelligently;
2. memory must never be retained without purpose.

RAM compression, working-set tracking, application freezing, cache reclamation, NUMA awareness where relevant, and pressure-aware policies are first-class design areas.

Disk-backed paging is a last layer of the hierarchy, not the first reaction to pressure.

---

## 3. Activity-aware scheduling

Aurora OS distinguishes between work that affects the user now and work that can wait.

Examples of scheduling classes may include:

- realtime;
- latency-critical;
- interactive;
- normal;
- background;
- idle;
- frozen.

The active user activity can influence scheduling priorities without requiring a manual "game mode" or similar switch.

Audio, input, rendering, and other latency-sensitive workloads receive explicit treatment.

---

## 4. The Desktop is the personal environment

The Desktop is the primary user environment and the main area intended for deep customization.

Users may customize, within safe system boundaries:

- layout;
- panels;
- docks;
- launch surfaces;
- widgets;
- shortcuts;
- visual themes;
- animation behavior;
- window behavior;
- Activity Spaces;
- multi-monitor layouts;
- gestures;
- information surfaces;
- web surfaces;
- AI surfaces where available.

System internals remain consistent even when the Desktop is heavily personalized.

The goal is to allow expressive personalization without fragmenting core OS behavior.

---

## 5. Activity Spaces

Aurora OS organizes work around persistent activities rather than treating every application window as an isolated object.

An Activity Space may contain:

- applications;
- documents;
- terminals;
- repositories;
- web surfaces;
- media;
- system state;
- project-specific context.

An Activity can be suspended and later resumed as a coherent workspace.

This model should eventually integrate with scheduling, memory freezing, restoration, and permissions.

---

## 6. The Web is a system capability

Internet access is integrated into the Desktop instead of being conceptually limited to a standalone browser application.

Aurora OS introduces **Web Surfaces**: isolated web-rendering surfaces that can appear as panels, windows, widgets, full-screen views, or parts of an Activity Space.

A Web Surface must not automatically gain access to:

- files;
- microphone;
- camera;
- clipboard;
- location;
- local applications;
- other Web Surfaces;
- OS identity;
- Activity data.

Access is capability-based and explicitly mediated.

A conventional browser application may still exist, but it is not the only gateway to the web.

---

## 7. Privacy by default

Privacy is the default system behavior, not an optional mode.

Principles:

- no mandatory online account;
- no mandatory telemetry;
- no advertising identifier;
- no behavioral profiling by the operating system;
- no silent upload of user content;
- no cloud dependency for basic OS functionality;
- permissions are explicit, scoped, and revocable;
- local execution is preferred for privacy-sensitive features;
- diagnostic data is opt-in and inspectable where practical.

---

## 8. Capability-based application security

Applications receive capabilities rather than broad implicit authority.

Examples:

- network access;
- microphone;
- camera;
- selected files or object scopes;
- clipboard read/write;
- GPU access;
- USB/device access;
- background execution;
- notifications;
- inter-process communication.

Capabilities should be narrow, revocable, and auditable.

Administrative privilege is not a substitute for fine-grained capability design.

---

## 9. System integrity and recovery

Core system state should be protected from arbitrary application modification.

The long-term direction includes:

- immutable or protected system components;
- signed packages;
- transactional installation and updates;
- atomic rollback;
- versioned system state;
- recovery environment;
- USB live boot;
- persistent live mode;
- installer and repair tooling derived from the same system image where practical.

A failed update should be recoverable without reinstalling the system.

---

## 10. Local-first architecture

Aurora OS must remain useful without internet connectivity.

Cloud services may extend the system but must not be required for:

- boot;
- login;
- local applications;
- local files;
- settings;
- recovery;
- core search;
- system administration.

---

## 11. Responsive by construction

The graphical interface must not block on slow operations.

Rendering, input, and animation should remain responsive while storage, networking, updates, indexing, or other services are active.

IPC and asynchronous APIs should be designed with this constraint in mind from the beginning.

---

## 12. No proprietary programming language

Aurora OS will not create or depend on a new proprietary programming language.

Kernel, drivers, services, and user-space components will use existing languages selected according to technical requirements.

The uniqueness of Aurora OS must come from its system architecture, behavior, security model, memory design, desktop model, and user experience.

---

## Guiding sentence

> Fast when active. Quiet when idle. Private by default. Recoverable by design. Personal where it matters.
