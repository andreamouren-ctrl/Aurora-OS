# G5 WP-05 — Reference camera implementation

Status: **initial user-space reference implementation, host acceptance pending**.
This is an incremental implementation task, not a WP-05 release/closure claim.

## Files and execution boundary
- `services/canvas/include/aurora/g5_canvas_camera.h` — bounded source API.
- `services/canvas/src/g5_canvas_camera.c` — user-space-only C11 math.
- `tests/g5/canvas_camera_test.c` — host negative, boundary, drift, stale revision and visibility tests.
- `.github/workflows/g5-wp05-camera.yml` — GCC/Clang strict+UBSan CI.

This code is **not linked into kernel Ring 0**: double precision, math library
and 128-bit intermediate logic belong in a future isolated Canvas Engine.
WP-03/WP-04 window clients still use the existing unchanged screen-space route.

## Numeric decisions for this reference implementation
- Persistent world positions: signed 64-bit ticks, 1024 ticks per unit.
- Camera: exact int64 anchor plus bounded double sub-tick offset.
- Unit conversion performs signed 128-bit subtraction before converting
  camera-relative deltas to double, including INT64_MIN/MAX.
- Finite zoom is limited to [1/64, 64] and physical device scale [0.25, 4].
- Viewport is 1..16384 pixels on either axis; per-operation pan is <= 65536 px.
- Compositor-facing coordinates are range-checked int32 pixels with deterministic
  nearest/half-away-from-zero rounding. The existing compositor is **not**
  claimed to support arbitrary double-scale painting yet.
- Camera revisions monotonically advance on accepted pan/zoom/resize and
  fail closed at UINT64_MAX. Pointer inverse/visibility queries must provide
  the exact revision. Scene revision is separate and belongs to WP-06.
- The AABB reference scan is conservative with 1px inflation; it does not
  allocate, mutate the scene, provide permissions or virtualize nodes.

## Required next implementation gates before WP-05 freeze
1. Integrate the library with a real isolated Ring 3 Canvas Engine process.
2. Publish an authorized camera revision to Shell/compositor input routing
   and reject stale generation and stale surface/window ownership.
3. Implement viewport pan/zoom and presentation with two real WP-04 windows,
   respecting the compositor's scale and 64-node ceiling.
4. Add deterministic QEMU first-boot/cold-reboot acceptance markers; do not
   waive the existing G5 WP-03/WP-04 gates.
5. Perform measured pixel/tick tolerance and sustained input latency tests,
   plus scene generation concurrency checks.

No filesystem format, persistent camera serialization, arbitrary zoom
composition or Canvas production UI is claimed by this first source slice.
