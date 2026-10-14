# G5 WP-05.A — Ring 3 camera arithmetic qualification gate

Status: **proposed implementation gate; not accepted; no runtime FP support claimed**.
Scope: issue #213, before integrating `services/canvas` into an executable Ring 3 Canvas Engine.

## Verified baseline
- `services/canvas/src/g5_canvas_camera.c` and `g5_canvas_projection.c` use `double`, `math.h`, and checked `__int128` world deltas; current tests are host GCC/Clang UBSan tests.
- Ordinary freestanding Ring 3 binaries are built without SSE/SSE2 and do not establish qualified per-thread FP/SIMD save/restore or libm support.
- The existing production pan path is bounded integer pixel translation over authenticated G5 IPC; preserve it unchanged.

## Architecture decision gate (must be resolved before runtime integration)
**Do not link the host camera reference directly into kernel or ordinary Ring 3.** Evaluate two implementation paths:
1. Ring 3 floating point: explicit architectural CPU feature discovery, XSAVE/FXSAVE strategy, per-thread state allocation and initialization, context-switch save/restore, user/kernel isolation, exception handling, compiler ABI, runtime/libm implementation and tests on SMP.
2. Audited integer/soft-float camera: preserve signed 64-bit persistent world ticks and the documented projection/roundtrip semantics, implement bounded overflow-checked intermediates, explicit rounding and zoom representation, and compare numerical tolerances and performance to the host oracle.

Select only after recording correctness, latency, memory cost, portability and security evidence. No implicit FP enablement, kernel FP use, ABI divergence or bypass of the existing Ring 3 isolation boundary.

## Mandatory negative and acceptance tests
- Cross-thread and cross-process camera-state contamination; scheduler preemption, migration and repeated creation/reap under SMP.
- Invalid NaN/infinity/zero/negative/out-of-range zoom (for FP path); extreme signed 64-bit anchors; arithmetic overflow and revision exhaustion.
- Deterministic pointer-anchored zoom and pan roundtrip at minimum/maximum zoom and device scale, including far-from-origin world positions.
- Stale camera revision, scene revision, object generation and session generation; ensure no partially published compositor frame or scene mutation.
- Deliberate Canvas Engine crash, restart and logout; revoke stale capabilities and reject messages from the previous process incarnation.
- First boot and cold reboot QEMU oracle with two independently owned WP-04 windows; unchanged WP-03/WP-04 and Identity gates.
- p50/p95 input-to-present latency, sustained gesture workload, memory footprint and numeric/pixel error budget, with recorded machine/QEMU configuration.

## Incremental delivery order
1. Produce a measured architecture decision record and standalone runtime arithmetic contract.
2. Implement the chosen arithmetic backend in a new isolated Ring 3 target; keep the host reference as an independent oracle.
3. Wire an authenticated camera-control path and monotonic revisions without widening kernel privileges.
4. Verify zoom, viewport projection and final compositor presentation; run the mandatory negative and recovery gates.
5. Only then propose WP-05 freeze, with evidence linked to exact tested commit and CI runs.

## Non-goals
This document does not implement FP context switching, a Canvas process, fractional compositor scaling, persistence, or full Living Canvas. It must not be cited as acceptance of WP-05.
