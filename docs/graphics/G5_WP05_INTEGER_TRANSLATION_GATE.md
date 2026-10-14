# G5 WP-05 — Integer translation safety gate (PR #219)

Status: **host-validated experimental slice; no Ring 3 integration or WP-05 freeze**.

## Implemented and tested

- Pure C11 integer-only translation at zoom=1 and device scale=1.
- World positions in signed 64-bit ticks; exactly 1024 ticks per pixel.
- Bounded signed deltas in [-32,32] pixels on both axes; zero/zero is rejected to match G5_OP_CANVAS_PAN.
- Fail-atomic updates with overflow/underflow and revision-exhaustion rejection.
- Explicit expected-revision check to reject stale requests.
- Strict GCC/Clang UBSan probe and differential test against the existing floating-point reference camera, across 207025 coordinate/delta combinations.

## Architectural boundaries

This probe does not authorize IPC, own window/surface handles, or create a
Canvas Engine process. Revision fencing alone does not replace generation,
session, process or capability ownership validation. Do not route untrusted
events into this helper without the Shell/compositor's existing authorization
and lifecycle controls. This helper has no zoom, fractional device scale,
fractional pixel motion, general inverse projection or clipping.

The differential test validates equivalence only in the overlapping
zoom=1/scale=1 integer-delta domain. It does **not** establish equivalence
for fractional zoom, fractional pixels or arbitrary scene coordinates.

## Required next gates

1. Keep PR #218 architecture decision independent: audited integer-only
   fixed-point scaling **or** isolated Ring 3 FP/SIMD context management.
2. Introduce an owned, synchronized Ring 3 Canvas Engine state with explicit
   generation/session/surface fencing and process teardown.
3. Integrate authorized input routing and two actual WP-04 windows, with
   deterministic QEMU first boot/cold boot acceptance and negative tests.
4. Verify all frozen WP-03/WP-04 regression gates and test lifecycle races.
5. Only then consider changing WP-05 acceptance status.

A successful host CI run is not proof of a successful QEMU or real-hardware
Canvas integration.

## Existing production integration point (audited)

The kernel already implements `G5_OP_CANVAS_PAN` in
`kernel/src/session/g5_shell_scene.c` and has a previously accepted
bounded two-window Ring 3 Shell IPC/QEMU gate (PR #214). Its 24-byte
payload carries signed pixel dx/dy, camera revision and scene revision;
the receiver validates session and surface generation, capability rights,
window policy, output bounds and compositor presentation with rollback.
The present experimental helper must **not** be treated as a replacement
for those authority checks or as a newly implemented Ring 3 integration.

The two models express movement in opposite coordinate frames: the
existing compositor moves window screen placements by +dx/+dy; a world
camera that tracks the same visual movement shifts its world anchor by
-dx/-dy. A future adapter must specify the sign convention explicitly
and must preserve both camera and scene revision semantics. The prototype
currently fences only camera revision; it does not implement the existing
scene revision or window policy transaction.
