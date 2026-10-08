# Aurora M4 Graphics Implementation Roadmap

Status: **Canonical implementation plan**
Version: **0.2**

This roadmap orders M4 work by hard dependencies. A checkbox in the global roadmap is completed only after the relevant implementation and runtime gate are satisfied.

## Phase G0 — Contracts

Status: **Complete (architecture only)**

- define graphics component ownership;
- define Display Service contract;
- define surface/buffer atomic commit model;
- define compositor responsibilities;
- define normalized graphical input routing;
- define window-management protocol;
- define Desktop Shell boundary;
- define Identity pre-session integration.

No runtime claim is implied.

## Phase G1 — Display foundation

Status: **Complete**

Implemented:

- [x] output object with bounded mode table and validated pixel geometry;
- [x] boot-framebuffer display backend adapter;
- [x] output and mode/geometry discovery registry with boot validation probe;
- [x] compositor-owned heap backbuffer with checked size/lifecycle;
- [x] safe bounded present operation for the boot-framebuffer backend;
- [x] monotonic presentation serials and explicit buffer-release signaling.

Foundation item set: **implemented**.

Acceptance gate: **runtime verified in QEMU**.

A Ring 3 graphics service repeatedly presents validated test frames through the capability-gated display contract without direct client framebuffer access. The validation path requires two monotonically increasing presentation serials and verifies synchronous release state.

## Phase G2 — Surface/buffer core

Status: **Complete**

Implemented:

- [x] bounded graphics-buffer object backed by refcounted shared memory;
- [x] capability-backed surface objects with explicit rights;
- [x] attach + bounded damage + atomic pending-to-committed publication.

Remaining:

- [x] capability-aware buffer lifetime, deferred destroy and safe object reuse;
- [x] bounded frame-callback core tied to commit/presentation serials;
- [x] capability-gated Ring 3 graphics-buffer map/unmap syscall path;
- [x] strict 32-bit RGB mask, stride, size and backing metadata validation;
- [x] Ring 3 frame-callback request/delivery ABI.

Acceptance gate:

Two isolated Ring 3 clients can independently submit surfaces; neither can map or corrupt the other's buffer.

Acceptance probe: **runtime verified in QEMU**. Two isolated Ring 3 clients now execute buffer map/unmap, surface attach/damage/commit and frame-callback request/delivery through the real SYSCALL/SYSRET path; cross-client handle attempts cannot map or attach the other client's buffer.

Audit closure:

- [x] capability-aware surface lifetime, destruction and slot recycling;
- [x] explicit cancellation/cleanup semantics for pending and queued frame callbacks when a surface is destroyed.

G2 acceptance gate and lifecycle hardening are runtime verified in QEMU.

## Phase G3 — Software compositor

Status: **Complete**

Implemented and runtime verified:

- [x] bounded capability-backed scene graph;
- [x] deterministic z-order;
- [x] output clipping with checked pixel addressing;
- [x] 32-bit packed RGB/RGBA CPU alpha composition;
- [x] translated surface damage aggregation;
- [x] persistent CPU backbuffer composition;
- [x] damage-driven output presentation;
- [x] frame-callback completion after successful presentation;
- [x] moving/overlapping surface QEMU correctness probe.

Implemented and runtime verified in the second/third G3 blocks:

- [x] bounded full-surface occlusion culling for fully covered opaque nodes;
- [x] nearest-neighbor integer scaling (1x-4x) with 0/90/180/270 transforms;
- [x] DISPLAY|CONTROL-gated privileged surface classes and secure-scene exclusion rules;
- [x] RGB10A2 composition into the SDR backbuffer;
- [x] 48-bit RGB12 composition into the SDR backbuffer;
- [x] RGBA16F decoding through the dedicated color-management engine;
- [x] direct 65,536-entry SMPTE ST.2084 EOTF path with exact 16-bit endpoint mapping;
- [x] BT.2100 reference HLG inverse-OETF + 1000-nit system-gamma path;
- [x] ICC v2/v4 RGB matrix-shaper import with sampled and parametricCurveType 0-4 TRCs;
- [x] monitor calibration through VCGT 1D ramps and optional bounded 17^3 3D LUT;
- [x] perceptual HDR shoulder, hue-preserving gamut compression and highlight chroma roll-off.

Acceptance gate:

Multiple moving/overlapping surfaces render correctly in QEMU with bounded memory growth and deterministic clipping tests.

Current gate status: **G3 runtime verified in QEMU**. Moving/overlapping surfaces, clipping, alpha, damage, occlusion, transforms/scaling, secure-scene exclusion and RGB10A2/RGB12/RGBA16F composition all pass the mandatory boot validation gate.

Color-management status: **mastering-grade matrix-shaper software path runtime verified in QEMU**. The kernel remains FPU/SIMD-free: PQ/HLG transfer evaluation, ICC transforms, calibration and tone mapping use generated LUTs plus bounded fixed-point math.

Final G3 mastering audit closure:

- [x] direct 65,536-entry ST.2084 LUT is exact at 0 and 10,000 nit and runtime-checks monotonicity over the complete 16-bit domain;
- [x] build-time numerical audit confirms Q16 PQ quantization error stays below 0.000008 nit absolute;
- [x] reference BT.2100 HLG path validates black, approximately 50.7-nit midpoint and approximately 1000-nit peak before target-output tone mapping;
- [x] ICC parsing is bounded by the profile-declared size, explicitly accepts v2/v4 matrix-shaper profiles and fails closed on unsupported versions/constructs;
- [x] sampled ICC curveType TRCs are required to be monotonic before bounded inversion; parametricCurveType 0-4 remains fixed-point and bounded;
- [x] singular ICC matrices fail closed; VCGT 1D calibration and optional 17^3 3D calibration remain bounded;
- [x] HDR tone mapping runtime-checks monotonic luminance, preserves the diffuse region below the shoulder, honors source peak/MaxCLL metadata and reaches the target peak without premature hard clipping;
- [x] out-of-gamut device-linear RGB remains signed until perceptual gamut compression, preventing destructive negative-channel pre-clipping and preserving the hue/chroma direction toward a bounded neutral;
- [x] final Bootstrap CI gate recompiles the freestanding -mno-sse/-mno-sse2 kernel and reaches the mandatory color-management self-test marker in four-CPU QEMU.

Explicit limits: ICC LUT-based A2B/B2A/CLUT profile transforms are not yet imported, and vendor GPU hardware LUT/degamma/gamma programming remains part of later native-GPU integration. The HLG path is the BT.2100 1000-nit reference OOTF followed by output-target tone mapping rather than vendor-display-specific hardware OOTF programming. These limits do not affect the current software compositor's calibrated matrix-shaper path.

## Phase G4 — Pointer and modern input

Status: **In progress**

Implemented and runtime-gated foundation:

- [x] normalized device-independent input event model;
- [x] sequenced/bounded normalized event queue;
- [x] PS/2 keyboard emits normalized keyboard events;
- [x] dedicated PS/2 mouse interrupt vector/stub;
- [x] IRQ12 PS/2 mouse packet decoder and live QEMU IRQ gate;
- [x] Identity credential handling remains isolated from pointer events;
- [x] secure-scene-aware compositor hit testing;
- [x] trusted graphics input router;
- [x] pointer focus routing;
- [x] keyboard focus routing;
- [x] private per-target event queues;
- [x] multi-client focus/no-leakage runtime gate.

Implemented and runtime-verified lifecycle hardening:

- [x] explicit owned pointer capture request/release semantics with foreign-release rejection;
- [x] immediate focus/capture revocation when a target surface is hidden or destroyed;
- [x] secure-scene policy changes revoke targets that are no longer hittable;
- [x] session teardown purges target queues and all graphics-input authority;
- [x] compositor teardown/restart notification revokes all registered input authority before compositor state destruction;
- [x] bounded per-target queue backpressure coalesces only immediately-consecutive pointer motion while preserving key/button/scroll ordering.

Implemented and runtime-verified modern-input decoder/binding foundation:

- [x] stable per-device identity in normalized input events;
- [x] transport-agnostic USB HID boot-keyboard report decoding with press/release state;
- [x] transport-agnostic USB HID boot-mouse motion/button/wheel decoding and device lifecycle events;
- [x] bounded USB HID binding registry with generational opaque handles;
- [x] validated protocol/report-size dispatch with stale-handle rejection;
- [x] disconnect sanitization publishes key/button releases before DEVICE_REMOVED.

Implemented and runtime-verified xHCI host-controller foundation:

- [x] bounded PCI capability-list walker;
- [x] live q35/qemu-xhci PCI class/BAR/MMIO capability discovery;
- [x] xHCI 1.0 capability parsing, context-size/scratchpad discovery and operational/runtime/doorbell base derivation;
- [x] halt -> HCRST -> Controller Not Ready clear sequence;
- [x] 4 KiB xHCI page-size support gate;
- [x] PMM-backed DCBAA, command ring, event ring and ERST setup;
- [x] polling-mode interrupter-0 event-ring bootstrap;
- [x] controller Run transition with mandatory QEMU runtime marker.

Remaining transport/extended-device work:

- [ ] command submission/completion starting with Enable Slot;
- [ ] port status/reset + USB device enumeration/addressing;
- [ ] control transfers for descriptors and HID interface/endpoint discovery;
- [ ] live interrupt-IN report delivery into the existing HID binding layer;
- [ ] MSI-X interrupt delivery after the polling command/event path is established;
- [ ] touch/pen/gamepad and accessibility/input-method layers later.

Acceptance gate:

Mouse and keyboard interact with multiple surfaces without cross-client event leakage.

Current G4 gate status: **runtime verified in four-CPU QEMU through xHCI controller discovery, reset and DMA-ring bootstrap**. In addition to the PS/2 and transport-independent HID routing/binding gates, Aurora now discovers a real emulated qemu-xhci controller, decodes its BAR/MMIO capabilities, resets it, verifies 4 KiB pages, programs DCBAA/command/event/ERST structures from PMM-owned physical pages and enters Run state. G4 remains **In progress** because Aurora has not yet submitted Enable Slot, enumerated USB devices or delivered live interrupt-IN HID reports into the existing HID binding layer.

## Phase G5 — Window protocol and Shell

Implement:

- toplevel configure/ack;
- activation tokens;
- placement/stacking policy;
- decorations;
- Desktop Shell process;
- launcher/panel/task switching baseline.

Acceptance gate:

A user can launch/switch/move/resize/close multiple native test windows through Shell policy rather than direct compositor privilege.

## Phase G6 — Identity migration

Implement:

- pre-session host;
- compositor-backed Aurora Identity System App;
- secure transition to authenticated session;
- lock/unlock presentation;
- compositor/Shell failure fallback to framebuffer recovery.

Acceptance gate:

Cold boot can authenticate through the normal compositor UI, enter a user desktop only after session readiness, lock/unlock safely, and still reach recovery login when the normal graphics stack is intentionally disabled.

## Phase G7 — Activity Spaces and persistence

Implement:

- Activity Space object model;
- Shell mapping of windows to spaces;
- persistent reconstructible workspace metadata;
- restore intents;
- customization framework.

Acceptance gate:

A reboot can restore Activity Space organization without granting stale process/surface capabilities.

## Phase G8 — Performance hardening

Measure and improve:

- frame latency;
- CPU time per damaged area;
- memory bandwidth/copies;
- idle wakeups;
- buffer count/memory;
- input-to-present latency.

Later acceleration may add GPU-backed rendering without replacing the client protocol.


## Extended color / HDR / output capability foundation

Status: **In progress**

Implemented foundation:

- [x] color primaries, transfer function, range and HDR static metadata are first-class buffer/mode metadata;
- [x] canonical 8-bit packed RGB/RGBA, RGB10A2, RGB12 and RGBA16F formats are validated;
- [x] output capabilities expose SDR/HDR/PQ/HLG/wide-gamut/VRR/DSC flags, bit-depth limits and VRR range.

Link/discovery foundation:

- [x] DDC / E-DDC transport abstraction with bounded EDID block reads;
- [x] DisplayPort/eDP AUX-DPCD base link capability path;
- [x] HDMI digital sink capability path through EDID/CTA;

Hardware/runtime integration still pending:

- [x] EDID base / CTA-861 HDR-color / DisplayID structural parsing;
- [ ] native HDMI / DisplayPort / eDP link backends;
- [x] HDMI VSDB/HF-VSDB FRL + validated VRR discovery foundation;
- [x] DisplayPort DPCD DSC/MST/128b132b-UHBR readiness discovery;
- [x] generic VRR range validation + presentation-policy hooks;
- [x] VRR / Adaptive-Sync backend programming contract;
- [x] detailed DSC capability/config validation model;
- [x] bounded HDMI/DP link-training state-machine contract;
- [ ] native GPU-specific VRR / Adaptive-Sync programming;
- [ ] native GPU-specific DSC programming;
- [x] hardware-facing display-controller mode-set/scanout contract;
- [x] HDMI/DP/eDP PHY/link backend contract over bounded training;
- [x] PCI display-class GPU driver registry with match/bind lifecycle;
- [x] QEMU Standard VGA / Bochs VBE native driver foundation;
- [x] runtime PCI display-class probe + BAR0 LFB validation;
- [x] native GPU candidate attachment with boot-framebuffer fallback preserved;
- [ ] vendor GPU scanout/color-pipeline programming;
- [x] mastering-grade software color conversion for matrix-shaper monitor profiles, including direct ST.2084, BT.2100 HLG, ICC TRCs, VCGT/3D calibration and perceptual HDR-to-SDR tone mapping;
- [ ] ICC LUT-based A2B/B2A profile import;
- [ ] vendor GPU hardware color-pipeline/LUT programming.
