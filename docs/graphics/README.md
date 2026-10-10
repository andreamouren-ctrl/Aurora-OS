# Aurora Graphics and Desktop Foundation

Status: **Canonical architecture + active implementation**
Version: **0.2**
Milestone: **M4 — Graphics and Desktop foundation**

This directory defines the contracts and implementation status for Aurora graphics, composition, windowing, input routing, Desktop Shell behavior and the transition from the bootstrap framebuffer path to the normal compositor-backed environment.

Architecture contracts remain authoritative even as the implementation evolves. Runtime status is tracked in [`IMPLEMENTATION_ROADMAP.md`](IMPLEMENTATION_ROADMAP.md).

## Current implementation status

At the 2026-10-10 accepted WP-04 merge on `main` (PR #185, merge `a45d3cab10544371d5f3535af0a768e6e0ea7e47`):

- **G0 — Contracts:** complete;
- **G1 — Display foundation:** complete/runtime verified;
- **G2 — Surface/buffer core:** complete/runtime verified;
- **G3 — Software compositor + mastering color pipeline:** complete/runtime verified;
- **G4 — Pointer and modern input:** complete/runtime verified for the V1 bounded-polling baseline;
- **G5 WP-03 — Ring 3 Shell bootstrap and session lifecycle:** frozen/runtime accepted;
- **G5 WP-04 — two independent Ring 3 window clients, focus, input, move/resize/close:** merged and runtime accepted (`4886658a` feature HEAD; five of five required workflows successful);
- **G5 WP-05+ — spatial camera, Living Canvas, full Shell UX:** pending; not part of WP-04 scope.

The current graphics implementation therefore goes substantially beyond the original architecture-only state.

## Architectural split

Normal target path:

```text
applications / system apps
        |
        v
window + surface protocol
        |
        v
Aurora Compositor <---- input routing / shell policy
        |
        v
Display Service
        |
        v
display backend / GPU / framebuffer
        |
        v
physical output
```

Recovery path:

```text
kernel / recovery environment
        |
        v
bootstrap framebuffer renderer
        |
        v
physical output
```

A compositor/display-service failure must not remove the ability to present recovery/login diagnostics when a usable boot framebuffer exists.

## Implemented foundations

Current G1-G3 foundations include:

- capability-gated Ring 3 display presentation;
- compositor-owned backbuffer;
- shared-memory graphics buffers;
- capability-backed surfaces;
- atomic attach/damage/commit;
- bounded object lifetime/recycling;
- frame callbacks;
- cross-client buffer/surface isolation;
- software scene composition;
- clipping, damage, z-order, transforms/scaling and occlusion;
- secure-scene exclusion;
- RGB10A2, RGB12 and RGBA16F source paths;
- direct ST.2084/PQ and BT.2100 HLG;
- ICC matrix-shaper profiles;
- VCGT/3D calibration;
- perceptual HDR tone mapping/gamut handling;
- display capability/link foundations for EDID/CTA/DisplayID, HDMI, DisplayPort, VRR and DSC;
- QEMU Standard VGA / Bochs VBE native-driver foundation.

G4 is complete for the current V1 bounded-polling baseline: secure hit testing, isolated pointer/keyboard focus, owned capture with lifecycle revocation, session/compositor teardown cleanup, bounded motion coalescing, live qemu-xhci keyboard and mouse, descriptor-driven wheel/five-button mouse handling, USB hot-unplug teardown and Event Ring quiescence are runtime verified. MSI-X interrupt delivery, touch/pen/gamepad, IME/layout/accessibility breadth and broader USB classes remain follow-on work rather than G4 V1 blockers.

G5 WP-03 and WP-04 are frozen and runtime accepted: the authenticated Ring 3 Shell/session generation and two independently running renderer clients are exercised through shared composition, trusted hit testing, focus/input, bounded move, resize/ACK and close plus stop/crash/reauthentication. WP-05 and later phases remain responsible for the spatial camera, Living Canvas, full decorations, launcher/task management, pre-session Identity System App integration and broader production desktop UX.

## Canonical documents

- [`ARCHITECTURE.md`](ARCHITECTURE.md) — component ownership, trust boundaries and current architecture state.
- [`DISPLAY_CONTRACT.md`](DISPLAY_CONTRACT.md) — physical-output and scanout contract.
- [`SURFACE_BUFFER_PROTOCOL.md`](SURFACE_BUFFER_PROTOCOL.md) — client surfaces, buffers, commits and damage.
- [`COMPOSITOR_CONTRACT.md`](COMPOSITOR_CONTRACT.md) — composition guarantees.
- [`INPUT_POINTER_CONTRACT.md`](INPUT_POINTER_CONTRACT.md) — keyboard/pointer routing and focus security.
- [`WINDOW_MANAGEMENT_CONTRACT.md`](WINDOW_MANAGEMENT_CONTRACT.md) — window roles, focus, placement and lifecycle.
- [`DESKTOP_SHELL_CONTRACT.md`](DESKTOP_SHELL_CONTRACT.md) — Desktop Shell policy and Activity Spaces.
- [`IDENTITY_INTEGRATION.md`](IDENTITY_INTEGRATION.md) — pre-session Identity graphics and recovery boundary.
- [`IMPLEMENTATION_ROADMAP.md`](IMPLEMENTATION_ROADMAP.md) — dependency-ordered M4 implementation and runtime status.

## Non-negotiable rules

1. Ordinary applications never map or own the physical display directly.
2. A client owns its content; the compositor owns composition; the Shell owns desktop policy.
3. Window-management policy is not embedded into the low-level Display Service.
4. Surface access is capability-scoped and session-scoped.
5. A surface commit is atomic from the client's point of view.
6. Input is delivered only to authorized targets selected by focus/capture policy.
7. Screen capture is a separate privileged capability.
8. The framebuffer login/recovery renderer remains independent of the normal desktop path.
9. GPU acceleration must not require changing the client authority model.
10. Designed, implemented, runtime-verified and real-hardware-certified states remain distinct.


The live USB mouse gate also verifies right-click, middle-click and vertical wheel scrolling in addition to motion and left-click. These are injected externally by QEMU and must emerge as normalized Aurora pointer/scroll events. Side/extra buttons are intentionally deferred to HID Report Descriptor support.


Aurora now reads the live mouse HID Report Descriptor and switches the interface to Report Protocol. The qemu USB mouse advertises five buttons, X/Y and a vertical wheel in a 32-bit report with no Report ID. Back/Forward decoding can therefore be implemented and runtime-tested against real emulated report bits rather than synthetic-only data.


Back/Forward are now supported by the descriptor-driven Report Mouse decoder. Aurora reads the real mouse Report Descriptor, which on QEMU advertises five buttons, then maps button 4 to Back and button 5 to Forward through the normal HID transport and normalized input queue. Because QEMU does not route side/extra host input into usb-mouse, the current Back/Forward gate is descriptor-derived runtime verification rather than external live side-button injection.


USB hot-unplug is now runtime verified: the workflow removes the qemu USB mouse through QMP and requires DEVICE_DELETED confirmation; Aurora then consumes the xHCI Port Status Change, emits normalized DEVICE_REMOVED, completes Disable Slot and releases the per-device DMA/context state. Report Mouse Back/Forward decoding is supported from the five-button descriptor layout, but is documented separately from the externally injected live-xHCI input gates.


G4 input transport is complete and runtime verified. Aurora now has live qemu-xhci keyboard and mouse input, normalized motion/buttons/wheel, descriptor-driven five-button mouse decoding, disconnect-driven device teardown and an explicit bounded-polling Event Ring baseline. The final QEMU gate verifies Event Ring quiescence after hot-unplug. MSI-X remains optional follow-on PCI/interrupt hardening and is not claimed as implemented.
