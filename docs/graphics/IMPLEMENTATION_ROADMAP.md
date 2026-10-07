# Aurora M4 Graphics Implementation Roadmap

Status: **Canonical implementation plan**
Version: **0.1**

This roadmap orders M4 work by hard dependencies. A checkbox in the global roadmap is completed only after the relevant implementation and runtime gate are satisfied.

## Phase G0 — Contracts

Status: **Complete (architecture only)**

- define graphics component ownership;
- define Display Service contract;
- define surface/buffer atomic commit model;
- define compositor responsibilities;
- define normalized graphical input routing;
- define window-management protocol;
- define Desktop Shell boundary;
- define Identity pre-session integration.

No runtime claim is implied.

## Phase G1 — Display foundation

Status: **In progress**

Implemented:

- [x] output object with bounded mode table and validated pixel geometry;
- [x] boot-framebuffer display backend adapter;
- [x] output and mode/geometry discovery registry with boot validation probe;
- [x] compositor-owned heap backbuffer with checked size/lifecycle;
- [x] safe bounded present operation for the boot-framebuffer backend;
- [x] monotonic presentation serials and explicit buffer-release signaling.

Foundation item set: **implemented**. The phase remains **In progress** until the Ring 3 acceptance gate below is satisfied.

Acceptance gate:

A Ring 3 graphics service can repeatedly present validated test frames through the display contract without direct client framebuffer access.

## Phase G2 — Surface/buffer core

Status: **In progress**

Implemented:

- [x] bounded graphics-buffer object backed by refcounted shared memory;
- [x] capability-backed surface objects with explicit rights;
- [x] attach + bounded damage + atomic pending-to-committed publication.

Remaining:

- [x] capability-aware buffer lifetime, deferred destroy and safe object reuse;
- [x] bounded frame-callback core tied to commit/presentation serials;
- [x] capability-gated Ring 3 graphics-buffer map/unmap syscall path;
- [x] strict 32-bit RGB mask, stride, size and backing metadata validation;
- [ ] Ring 3 frame-callback request/delivery ABI.

Acceptance gate:

Two isolated Ring 3 clients can independently submit surfaces; neither can map or corrupt the other's buffer.

Acceptance probe: **implemented; runtime verification pending CI**. Ring 3 surface attach/damage/commit syscall ABI is now defined.

## Phase G3 — Software compositor

Implement:

- scene graph;
- z-order;
- clipping;
- alpha composition;
- damage tracking;
- CPU backbuffer composition;
- output presentation;
- secure privileged surface classes.

Acceptance gate:

Multiple moving/overlapping surfaces render correctly in QEMU with bounded memory growth and deterministic clipping tests.

## Phase G4 — Pointer and modern input

Implement:

- mouse/pointer device path;
- normalized device-independent events;
- compositor hit testing;
- pointer focus;
- keyboard focus routing;
- focus/capture revocation;
- USB HID input foundation where transport dependencies are ready.

Acceptance gate:

Mouse and keyboard interact with multiple surfaces without cross-client event leakage.

## Phase G5 — Window protocol and Shell

Implement:

- toplevel configure/ack;
- activation tokens;
- placement/stacking policy;
- decorations;
- Desktop Shell process;
- launcher/panel/task switching baseline.

Acceptance gate:

A user can launch/switch/move/resize/close multiple native test windows through Shell policy rather than direct compositor privilege.

## Phase G6 — Identity migration

Implement:

- pre-session host;
- compositor-backed Aurora Identity System App;
- secure transition to authenticated session;
- lock/unlock presentation;
- compositor/Shell failure fallback to framebuffer recovery.

Acceptance gate:

Cold boot can authenticate through the normal compositor UI, enter a user desktop only after session readiness, lock/unlock safely, and still reach recovery login when the normal graphics stack is intentionally disabled.

## Phase G7 — Activity Spaces and persistence

Implement:

- Activity Space object model;
- Shell mapping of windows to spaces;
- persistent reconstructible workspace metadata;
- restore intents;
- customization framework.

Acceptance gate:

A reboot can restore Activity Space organization without granting stale process/surface capabilities.

## Phase G8 — Performance hardening

Measure and improve:

- frame latency;
- CPU time per damaged area;
- memory bandwidth/copies;
- idle wakeups;
- buffer count/memory;
- input-to-present latency.

Later acceleration may add GPU-backed rendering without replacing the client protocol.


## Extended color / HDR / output capability foundation

Status: **In progress**

Implemented foundation:

- [x] color primaries, transfer function, range and HDR static metadata are first-class buffer/mode metadata;
- [x] canonical 8-bit packed RGB/RGBA, RGB10A2, RGB12 and RGBA16F formats are validated;
- [x] output capabilities expose SDR/HDR/PQ/HLG/wide-gamut/VRR/DSC flags, bit-depth limits and VRR range.

Hardware/runtime integration still pending:

- [x] EDID base / CTA-861 HDR-color / DisplayID structural parsing;
- [ ] native HDMI / DisplayPort / eDP link backends;
- [ ] VRR / Adaptive-Sync programming;
- [ ] DSC programming;
- [ ] GPU scanout/color-pipeline programming;
- [ ] compositor color conversion and HDR tone mapping.
