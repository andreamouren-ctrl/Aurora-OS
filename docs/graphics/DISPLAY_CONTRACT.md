# Aurora Display Service Contract

Status: **Canonical architecture contract**
Version: **0.1**

## Purpose

The Display Service abstracts physical display outputs and presentation from the compositor. It is the only normal user-space graphics component allowed to control physical scanout state.

## Output object

Each output exposes a stable runtime identifier and immutable/current properties:

- physical mode dimensions;
- refresh numerator/denominator or equivalent precise timing;
- pixel format;
- stride/alignment constraints;
- logical scale;
- transform/orientation;
- connection state;
- presentation capability flags.

Output identifiers are runtime identities, not permanent hardware serial identities.

## Required operations

The initial logical contract includes:

- enumerate outputs;
- query supported modes;
- select a mode;
- allocate or register a scanout-compatible target;
- present a completed frame;
- receive presentation completion/timestamp;
- blank/unblank output;
- release resources.

Mode changes are privileged and atomic from compositor clients' point of view.

## Ownership

Only the Display Service owns backend-specific display state. The compositor receives a narrowly scoped presentation capability. Applications never receive direct output control.

## Initial backend

The first backend may present through the boot-provided linear framebuffer.

That backend is an implementation bridge, not the permanent architecture. The contract must permit later DRM-like/native GPU display backends without changing client surface semantics.

## Presentation rules

- A frame must not be exposed as complete before its scanout/present contract is accepted.
- Buffer reuse must not occur until the Display Service signals safe release.
- Width, height, stride and arithmetic are overflow-checked.
- Unsupported formats/modes fail closed.
- Hot-unplug invalidates the affected output and wakes interested trusted clients.

## Multi-output direction

The model is output-centric from v0.1 even if the first runtime target exposes only one output. The compositor must not hard-code one global framebuffer into its public ABI.

## Recovery boundary

The kernel/bootstrap framebuffer renderer is not a client of this service. Recovery rendering can remain available if the normal Display Service cannot start.
