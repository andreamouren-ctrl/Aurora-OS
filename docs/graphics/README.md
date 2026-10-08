# Aurora Graphics and Desktop Foundation

Status: **Canonical architecture + active implementation**
Version: **0.2**
Milestone: **M4 — Graphics and Desktop foundation**

This directory defines the contracts and implementation status for Aurora graphics, composition, windowing, input routing, Desktop Shell behavior and the transition from the bootstrap framebuffer path to the normal compositor-backed environment.

Architecture contracts remain authoritative even as the implementation evolves. Runtime status is tracked in [`IMPLEMENTATION_ROADMAP.md`](IMPLEMENTATION_ROADMAP.md).

## Current implementation status

At the 2026-10-07 audit baseline:

- **G0 — Contracts:** complete;
- **G1 — Display foundation:** complete/runtime verified;
- **G2 — Surface/buffer core:** complete/runtime verified;
- **G3 — Software compositor + mastering color pipeline:** complete/runtime verified;
- **G4 — Pointer and modern input:** active;
- **G5+ — Window protocol, Desktop Shell, Identity compositor migration and Activity Spaces:** future/next phases.

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

G4 now adds the runtime-verified PS/2 + normalized routing foundation: secure hit testing, isolated pointer/keyboard focus, owned capture with lifecycle revocation, session/compositor teardown cleanup and bounded motion coalescing under queue pressure. Normalized events carry stable device identity, and transport-independent USB HID boot keyboard/mouse decoders plus a bounded generational binding/dispatch layer are runtime verified in QEMU. The hardware-facing path has also advanced: Aurora discovers qemu-xhci through PCI, maps BAR0 MMIO, initializes controller DMA structures, resets a connected USB port, completes Enable Slot and Address Device, and then performs real EP0 control transfers. Runtime validation reads VID 0x0627 / PID 0x0001, USB 2.0, identifies the qemu keyboard as HID Boot protocol 1 on endpoint 0x81, sends SET_CONFIGURATION + SET_PROTOCOL(Boot), configures the xHCI interrupt endpoint and receives a live 8-byte report. The QEMU gate injects key A and Aurora observes usage 0x04, then routes it through the existing HID binding layer into normalized input. G4 remains in progress for second-device/live mouse enumeration and actual port-disconnect handling. The cleanup path itself is now runtime verified: HID unbind, Disable Slot completion, DCBAA clear and per-device EP0/HID/context DMA release all complete successfully after the live keyboard gate.

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
