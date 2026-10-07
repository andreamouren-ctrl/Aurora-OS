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
- [x] output and mode/geometry discovery registry with boot validation probe.

Remaining:

- [ ] compositor-owned backbuffer;
- [ ] safe present operation;
- [ ] presentation/release signaling.

Acceptance gate:

A Ring 3 graphics service can repeatedly present validated test frames through the display contract without direct client framebuffer access.

## Phase G2 — Surface/buffer core

Implement:

- shared/bounded graphics buffer primitive;
- capability-backed surface handles;
- attach/damage/commit;
- buffer release;
- frame callbacks;
- strict metadata/bounds validation.

Acceptance gate:

Two isolated Ring 3 clients can independently submit surfaces; neither can map or corrupt the other's buffer.

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
