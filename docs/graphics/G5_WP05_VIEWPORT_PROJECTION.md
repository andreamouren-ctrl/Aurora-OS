# G5 WP-05 — Bounded viewport projection reference

Status: **host-only source candidate; no production compositor bridge yet**.

This incremental reference library consumes the numeric D36 camera state
and produces 1-pixel-inflated, clipped screen-space rectangles suitable for
eventual authorization and presentation by G5 Window Policy.

- World AABBs may cross INT64_MIN..INT64_MAX. Signed 128-bit subtraction
  prevents undefined overflow before camera-relative double conversion.
- Projection clips in double space **before** conversion to compositor int32
  coordinates, including a world AABB far larger than the current viewport.
- Panel IDs and object generations must be nonzero. IDs must be unique
  within a batch. Session and scene revisions must be supplied and validated.
  Neither is proof of authorization; the trusted Shell still checks capability
  ownership and generation before committing compositor operations.
- A failed batch never writes a partial projected visible set. This
  reference currently caps input and output at 64 entries, respecting
  the G3 compositor's bounded node limit. More logical nodes require the
  later WP-06/WP-07 virtualization and spatial-index work.
- Current G5 WP-04 framebuffer windows are NOT being moved or resized by
  this library. There is no Ring 3 Canvas process integration, scene graph,
  persistent camera, or QEMU presentation gate. Do not mark WP-05 complete.

Next gate: wire a real shell-owned, generation-fenced source of camera
commands through existing G5 IPC, validate surface caps/ownership,
integrate the visible-set projection and render two independent Ring 3
surfaces in QEMU with pan/zoom while preserving WP-04 recovery evidence.
Also decide on user-space floating-point context preservation or an
audited soft-float/fixed-point implementation before using this library in
the freestanding Ring 3 runtime.
