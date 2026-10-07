# ADR-0006 — Aurora graphics compositor and desktop split

Status: **Accepted**
Date: **2026-10-07**

## Context

Aurora already has direct framebuffer rendering for boot, diagnostics and the Identity bootstrap prototype. M4 requires a normal desktop graphics architecture without turning the framebuffer renderer into a monolithic desktop server.

The project also requires capability-based isolation, recoverability, low latency, deep customization and a permanent recovery login path.

## Decision

Aurora adopts the following logical split:

1. **Display Service** owns physical outputs and presentation.
2. **Aurora Compositor** owns surfaces and composition.
3. **Desktop Shell/window policy** owns placement, focus policy, system chrome and Activity Spaces.
4. **Applications/System Apps** render into capability-scoped buffers and atomically commit them to surfaces.
5. **Aurora Identity System App** uses a privileged pre-session surface role in the normal graphics stack.
6. **Bootstrap/Recovery framebuffer UI** remains independent of the compositor path.

Early implementation may co-locate Display Service and compositor code in one Ring 3 process, but the contracts and privilege boundaries remain distinct.

The first compositor may be software/CPU based. The protocol must permit later GPU acceleration and multi-output support.

## Consequences

Benefits:

- applications never need direct display access;
- compositor mechanism is separated from desktop product policy;
- Shell can evolve and be customized without changing buffer security;
- graphics services can be restarted independently of the kernel;
- Identity retains a recovery path when graphics services fail;
- later acceleration does not require redesigning application surface semantics.

Costs:

- more IPC/object lifecycle design than a monolithic framebuffer desktop;
- synchronization and buffer-release rules are required;
- Shell/compositor restart behavior must be explicitly tested.

## Rejected alternatives

### Direct framebuffer access by applications

Rejected because it prevents isolation, reliable composition and secure overlays.

### Compositor and Desktop Shell as one permanent architectural object

Rejected because it mixes rendering mechanism with desktop policy and makes customization/recovery harder.

### GPU-first API as M4 prerequisite

Rejected because it delays functional desktop bring-up and couples architecture to unfinished hardware drivers.
