# Aurora Compositor Contract

Status: **Canonical architecture contract**
Version: **0.1**

## Purpose

The Aurora Compositor transforms a validated scene of surfaces into frames for one or more outputs. It is a mechanism component, not the owner of desktop product policy.

## Responsibilities

The compositor owns:

- surface object lifecycle;
- committed scene state;
- stacking data supplied by authorized policy;
- clipping and occlusion;
- transforms and scaling;
- alpha composition;
- damage tracking;
- cursor composition where configured;
- frame scheduling;
- presentation submission;
- frame callbacks and buffer release;
- secure hiding/removal of revoked surfaces.

## Not compositor responsibilities

The compositor does not decide:

- which app should be launched;
- Activity Space semantics;
- taskbar/panel design;
- application permissions;
- account authentication;
- package lifecycle;
- arbitrary access to client files.

## Scene transaction

Policy changes affecting multiple surfaces should be expressible as one scene transaction so a desktop transition does not expose intermediate states.

A transaction may include:

- visibility;
- z-order;
- position;
- size constraints;
- output assignment;
- focus target metadata;
- transform.

## Composition algorithm

The first implementation may use CPU raster composition into a software backbuffer.

Required correctness properties:

1. clip every source/destination rectangle;
2. use overflow-checked address calculations;
3. never read beyond a client buffer;
4. apply opacity/alpha consistently;
5. never expose uninitialized compositor memory;
6. present only completed frames.

Optimization may later add occlusion culling, SIMD, tiled damage, GPU textures and direct scanout.

## Frame scheduling

The compositor maintains a monotonic frame timeline. Frame callbacks are advisory presentation opportunities, not hard real-time guarantees.

Interactive surfaces should receive scheduling priority consistent with Aurora's “interactive work wins” architecture principle.

## Secure surfaces

Pre-session, lock-screen and security-sensitive overlays are privileged surface classes. While active, ordinary session surfaces cannot receive input and may be fully excluded from composition according to lock policy.

## Capture

Output/surface capture is a distinct privileged path. The compositor must require an explicit capture capability and expose capture activity to higher-level privacy UI when the feature is implemented.

## Crash/restart

On restart, the compositor starts from no trusted client scene. Clients must reconnect/recreate surfaces or be restored through an explicit session protocol. Stale handles from the prior compositor instance are invalid.
