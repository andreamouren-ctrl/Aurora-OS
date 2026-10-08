# Aurora Graphics Architecture

Status: **Canonical architecture contract with G1-G3 runtime implementation**
Version: **0.2**

## 1. Goal

Aurora M4 defines a capability-scoped graphics stack that can evolve from the current software compositor into accelerated rendering without changing application-facing ownership or security contracts.

The first three implementation phases are no longer hypothetical:

- **G1 Display foundation — complete/runtime verified**
- **G2 Surface/buffer core — complete/runtime verified**
- **G3 Software compositor + professional color pipeline — complete/runtime verified**
- **G4 Pointer/modern input — active**

## 2. Components

### Display Service

Owns physical display outputs, modes, scanout targets, presentation timing and backend-specific display state. It does not own desktop policy.

Current implementation includes output/mode objects, boot-framebuffer presentation, display-controller abstractions, DDC/EDID capability discovery, HDMI/DisplayPort contracts and a QEMU Standard VGA/Bochs VBE driver foundation.

### Aurora Compositor

Owns composition of authorized surfaces into output frames. It tracks scene state, clipping, transforms, visibility, damage, presentation and frame-callback completion.

The current G3 software compositor is runtime verified for:

- deterministic z-order;
- clipping;
- alpha composition;
- damage aggregation;
- transforms and integer scaling;
- bounded occlusion culling;
- secure-scene exclusion;
- RGB10A2, RGB12 and RGBA16F source handling;
- calibrated software color conversion.

### Window Manager / Shell policy

Owns placement, stacking policy, focus policy, workspaces/Activity Spaces, decorations and system chrome.

This policy remains a G5+ responsibility. It must remain logically separate from low-level composition even if early implementations share process infrastructure.

### Desktop Shell

Trusted user-session component that will implement panels, launcher, task switching, Activity Spaces, notifications, system overlays and customization.

The Desktop Shell is not yet complete.

### Clients

Applications and system apps render into their own buffers and submit those buffers to compositor-managed surfaces.

Two isolated Ring 3 clients are already runtime tested against the surface/buffer capability model.

## 3. Process model

Target production split:

```text
Display Service         privileged Ring 3 service
Aurora Compositor       privileged/session graphics service
Desktop Shell           trusted session client + policy authority
Applications            untrusted clients
Identity System App     trusted pre-session client
```

Early M4 may combine some Display/Compositor implementation while preserving public contracts and authority boundaries.

The boot/recovery framebuffer path remains independent of this normal-session split.

## 4. Capability model

Clients receive explicit capabilities for the graphics objects they may use. Possessing a generic IPC endpoint does not imply graphics authority.

Logical rights include:

- CREATE_SURFACE
- ATTACH_BUFFER
- COMMIT
- REQUEST_FRAME_CALLBACK
- SET_ROLE
- RECEIVE_INPUT
- REQUEST_ACTIVATION
- CAPTURE_OUTPUT
- MANAGE_WINDOWS
- MANAGE_OUTPUTS

Ordinary applications must never receive MANAGE_OUTPUTS, MANAGE_WINDOWS or CAPTURE_OUTPUT merely because they can render a normal surface.

Current G2 validation includes cross-client rejection: one Ring 3 client cannot map or attach another client's buffer without the appropriate capability.

## 5. Session isolation

Every normal surface belongs to exactly one session security domain.

A surface from one authenticated session must never become visible/readable in another session without an explicit trusted transition protocol.

On logout or terminal session failure, graphics authority must be revoked before session destruction is considered complete.

This remains a required integration gate as the Desktop Shell and compositor become session-owned services.

## 6. Color and HDR architecture

HDR and color management are no longer "out of scope".

The current software path includes:

- SDR/wide-gamut/HDR metadata models;
- SMPTE ST.2084/PQ;
- BT.2100 HLG;
- ICC v2/v4 RGB matrix-shaper import;
- sampled and parametric TRCs;
- monitor VCGT calibration;
- optional bounded 17^3 calibration LUT;
- perceptual HDR shoulder/tone mapping;
- hue-preserving gamut compression;
- highlight chroma roll-off.

The current limits are explicit:

- ICC LUT-based A2B/B2A profile transforms are not yet imported;
- vendor GPU hardware degamma/gamma/3D-LUT programming is not yet implemented;
- output HDR signaling ultimately depends on native vendor display programming;
- current validation is primarily the software compositor/QEMU path.

## 7. Output/link architecture

Implemented foundations include:

- DDC / E-DDC abstraction;
- EDID base parsing;
- CTA-861 HDR/color discovery;
- DisplayID structural parsing;
- DisplayPort AUX/DPCD capability discovery;
- HDMI digital sink capability parsing;
- HDMI VSDB/HF-VSDB FRL/VRR discovery;
- DisplayPort DSC/MST/UHBR readiness discovery;
- VRR policy/backend contracts;
- DSC validation/config model;
- bounded HDMI/DP link-training state-machine contracts;
- display-controller mode-set/scanout contract;
- GPU display-driver match/bind registry;
- QEMU Standard VGA / Bochs VBE foundation.

Still required are production vendor-specific scanout/link/color/VRR/DSC backends.

## 8. Input relationship

Graphics consumes normalized device-independent input events rather than device-specific PS/2 packet formats.

G4 currently includes:

- normalized keyboard and pointer events;
- PS/2 keyboard plus IRQ12 PS/2 mouse decoding;
- secure-scene-aware compositor hit testing;
- isolated keyboard and pointer focus routing;
- owned/revocable pointer capture;
- immediate revocation for hidden/destroyed surfaces and secure-scene transitions;
- full session/compositor teardown revocation;
- bounded per-target queues with adjacent motion coalescing that preserves key/button/scroll ordering;
- stable device identity carried by normalized input events;
- transport-independent USB HID boot keyboard/mouse report decoders with device lifecycle events;
- bounded HID binding registry with generational handles and stale-reference rejection;
- strict report-size/protocol dispatch and pressed-state sanitization before removal;
- bounded PCI capability-list traversal for controller feature discovery;
- live qemu-xhci PCI/BAR/MMIO discovery and xHCI 1.0 capability parsing;
- xHCI halt/reset/readiness and 4 KiB page-size validation;
- PMM-backed DCBAA, command ring, event ring and ERST programming;
- polling-mode interrupter-0 bootstrap and verified controller Run transition;
- connected-port detection/reset on live qemu-xhci;
- command-ring Enable Slot submission and event-ring Command Completion validation;
- asynchronous Port Status Change event consumption while waiting for command completions;
- context-size-aware Input/Device Context construction;
- PMM-backed EP0 transfer-ring provisioning;
- Address Device command completion with controller-populated USB address validation;
- EP0 Running-state validation from the resulting Device Context;
- bounded EP0 Setup/Data/Status control-IN transfers with Transfer Event validation;
- Device Descriptor parsing with USB version and VID/PID extraction;
- bounded Configuration/Interface/Endpoint descriptor parsing;
- HID Boot interface classification and interrupt-IN endpoint discovery;
- SET_CONFIGURATION and HID SET_PROTOCOL(Boot) control requests;
- interrupt-IN Endpoint Context construction and Configure Endpoint command;
- dedicated HID transfer ring with Transfer Event residual/completion validation;
- live report binding into the existing USB HID transport and normalized input queue;
- QEMU sendkey runtime injection proving key-A delivery from the emulated USB keyboard;
- connected-port iteration for subsequent devices;
- HID unbind followed by xHCI Disable Slot completion;
- DCBAA visibility removal and PMM release of per-device Input/Device Context, EP0 and HID rings;
- separation between pointer events and Aurora Identity credential input.

The current G4 foundation is runtime verified end-to-end for the first live qemu USB HID Boot keyboard. The observed 8-byte report is 00 00 04 00 00 00 00 00 and is decoded into the normalized key-A event. G4 is still not complete: Aurora currently stops after the first connected HID device. The per-device teardown mechanism is runtime verified, but second-device enumeration/live mouse input and teardown triggered by a real port-disconnect event remain before the input transport can be considered complete. Touch, pen, gamepad, accessibility and input-method layers remain later work.

## 9. Performance direction

The protocol is designed for:

- damage-based recomposition;
- double/triple buffering where useful;
- bounded copies;
- buffer reuse;
- asynchronous frame callbacks;
- frame pacing;
- later zero-copy/GPU-backed buffers;
- later multi-output support;
- GPU acceleration without changing the client ownership contract.

Correctness must not depend on GPU acceleration.

## 10. Failure behavior

If the compositor or Display Service fails:

- ordinary client presentation stops safely;
- stale clients must not gain new input;
- session graphics capabilities must be invalidated/reconstructed;
- trusted supervision may restart graphics services with fresh authority;
- boot/recovery framebuffer output remains outside this dependency chain.

## 11. Remaining major work

The following are not yet production-complete:

- vendor GPU acceleration;
- GPU scheduling/virtual memory;
- production multi-monitor/hotplug;
- G4 USB HID and extended modern-input transports/classes;
- G5 window protocol;
- Desktop Shell;
- normal compositor-backed Identity pre-session UI;
- 3D application API;
- remote desktop;
- explicit cross-session sharing policy if ever enabled.

These may be added without weakening the base ownership model.


G4 Boot Mouse live coverage now includes relative motion, left/right/middle-button transitions and vertical wheel scroll. The runtime gate injects these through the QEMU monitor and validates that they traverse qemu-xhci, the interrupt-IN transfer ring, the USB HID transport and the normalized Aurora input queue. Additional side/extra mouse buttons remain a Report-Protocol concern rather than part of the fixed Boot Mouse contract.


The xHCI HID path now reads and parses the HID Report Descriptor instead of relying exclusively on Boot Protocol. The live QEMU mouse descriptor resolves to a 32-bit input report with Report ID 0, X/Y, vertical wheel and five button bits. Aurora also successfully issues HID SET_PROTOCOL(Report). This establishes the descriptor-driven foundation required for Back/Forward and other non-Boot controls without hardcoded report assumptions.


Aurora's USB HID mouse path now supports a descriptor-driven five-button Report Mouse in addition to fixed Boot Mouse decoding. The generic HID layer owns the report layout and arbitrary-bit decoder; xHCI is responsible only for acquiring the descriptor and delivering bytes. Buttons 4 and 5 map to normalized Back/Forward events, while X/Y/wheel are decoded from the actual Report Descriptor offsets and sizes.

The QEMU mouse advertises exactly five buttons in a 32-bit report and accepts SET_PROTOCOL(Report). QEMU's monitor/input routing cannot currently inject side/extra buttons through usb-mouse, so the Back/Forward runtime gate uses the live descriptor-derived layout to construct conforming reports inside Aurora and verifies the complete HID transport/decoder/normalized-input path. This distinction is intentionally documented to avoid overstating emulated-hardware coverage.


G4 now includes disconnect-driven HID teardown. A QMP hot-unplug of the live qemu USB mouse produces an xHCI Port Status Change Event; Aurora validates the disconnected PORTSC state, acknowledges the change bits, unbinds the HID device so DEVICE_REMOVED is emitted, then disables the xHCI slot and releases the DCBAA/device-context/input-context/EP0/HID-ring resources. The event consumer tolerates transfer events that may be generated as the removed endpoint is retired before the port-change event becomes visible.

Descriptor-driven Report Mouse decoding supports five button bits and maps buttons 4/5 to Back/Forward. That decoder path is verified against the live Report Descriptor layout, while live external side-button injection remains distinct from the hot-unplug gate.


G4 now includes a disconnect-driven xHCI lifecycle. A live QEMU USB mouse is hot-unplugged externally; Aurora consumes the resulting Port Status Change Event for the device root port, validates disconnected PORTSC state, acknowledges RW1C change bits, unbinds the HID transport so normalized DEVICE_REMOVED is emitted, then disables the xHCI slot and releases its DCBAA/context/ring resources. This establishes hardware-event-driven authority revocation rather than relying only on an explicit software teardown call.
