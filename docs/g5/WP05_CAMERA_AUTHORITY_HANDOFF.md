# WP-05.A — Camera authority and Ring 3 synchronization gate

Status: **OPEN — NOT FROZEN**.

## Implemented protocol

1. The trusted kernel input router recognizes eligible USB HID background scroll input and allocates a monotonic request ID. **This is the old experimental scroll-to-pan mapping, not the newly approved Aurora OS interaction design.**
2. Ring 3 receives `CANVAS_PAN_REQUEST` with expected kernel camera/scene revisions and stages `g5_icam_pan()` on a temporary camera.
3. Ring 3 forwards `G5_OP_CANVAS_PAN` and sends `CANVAS_PAN_ACK`. This ACK means *forwarded*, not *committed*.
4. Kernel polls G5 IPC, checks successful compositor transaction and revision advancement, then sends `CANVAS_PAN_COMMITTED` with authoritative revisions. On failure it sends `CANVAS_PAN_REJECTED`.
5. Ring 3 commits its staged camera only on matching `COMMITTED`; it discards it on matching `REJECTED`. Mismatched request IDs/revisions fail closed.

## Approved interaction direction — contextual controls

The user-approved navigation design **supersedes the experimental wheel-to-pan mapping**:

| Context / gesture | Intended behavior |
| --- | --- |
| Canvas navigation, wheel up/down | Zoom in/out, anchored at pointer |
| Canvas navigation, held W/A/S/D or arrow keys | Continuous camera movement |
| Empty canvas, left-button drag | Smooth camera pan; release ends drag |
| Right button held + wheel | Cycle open applications, previous/next |
| Release right button after cycling | Confirm application and navigate camera to its window |
| Text editor / menu / application-owned controls | Preserve application input; do not hijack typing, text selection or app scrolling |

The context resolver must prioritize focused application controls and explicit modal operations, with a clearly defined canvas-navigation mode. Right-button + wheel is a chord, not an ordinary right-click: do not open the context menu after the chord has been consumed. A plain right-click remains available to applications. Direction and precise wheel-step mapping must be verified with real devices and accessibility settings.

## Quick-switch camera policy

Default proposal: keep each window at its world position, select an application while the right button is held, then move the camera to the selected window on release. Use a short optional animation (target 150–250 ms) with reduced-motion and performance-sensitive immediate-jump fallback. Do **not** reposition windows as a shortcut for navigating the camera.

This is a **target architecture**, not a current implementation. The present G5 prototype physically translates two windows during pan; a compositor camera transform, app enumeration, thumbnail preview, contextual input dispatch, keyboard repeat and drag capture are still to be designed/implemented. An animated camera is generally more work than a simple z-order switch; any resource-saving claim requires benchmarks.

## Outstanding engineering gates

- Replace the legacy scroll-to-pan input route and its QEMU oracle with context-aware wheel-to-zoom, keyboard pan, drag pan, and right-button-wheel switch tests.
- Define a kernel-authoritative camera snapshot (center, zoom, viewport, revision) and reconcile it across resize, rejection, restart and out-of-order messages; do not confuse a committed delta with an entire camera snapshot.
- Make rendering camera-relative rather than moving world-space windows. Specify clipping, hit testing, focus, accessibility, frame pacing and offscreen culling.
- Preserve existing capability/security checks, input provenance, and IPC revision validation.
- Measure CPU, GPU, latency and memory against z-order-only switching on representative hardware.
- Run end-to-end CI/QEMU; do not freeze WP-05.A while the bootstrap smoke gate is red.

## CI status at last review

The integer-camera host workflow passed at commit `8f446985`. The QEMU BIOS four-CPU bootstrap failed at WP-05 input probe stage 6 (first USB HID wheel-to-pan) on that commit. Commit `d3d9b498` added detailed diagnostics; results are not yet established in this document.
