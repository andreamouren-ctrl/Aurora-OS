# Aurora M4 Graphics Implementation Roadmap

Status: **Canonical implementation plan**
Version: **0.2**

This roadmap orders M4 work by hard dependencies. A checkbox in the global roadmap is completed only after the relevant implementation and runtime gate are satisfied.

## G5 WP-01 — Source audit baseline (2026-10-08)

- [x] Targeted current-source inspection and P0/P1 risk register: [G5_WP01_REPOSITORY_AUDIT.md](G5_WP01_REPOSITORY_AUDIT.md).
- [ ] Fresh baseline build + QEMU CI artifact validation (not executed by this documentation audit).
- [ ] Review/freeze actual Shell capability mapping and least-privilege launch path.

- [x] **WP-02 QEMU regression unblock (2026-10-08):** PR #139 all four workflows SUCCESS (Entropy Source Policy, Identity Ring 3, G5 cold boot recovery, x86_64 Bootstrap). Diagnosed RDSEED eighth-startup-sample transient failure; bounded retries + negative host tests preserve mandatory fail-closed entropy validation. Commit `75a1fbf0` correction; `aa6090eb` acceptance evidence. This verifies G5 Session Host bootstrap/IPC integrity, not a finished graphical Shell/compositor.
- [ ] **WP-02 EXIT GATE (2026-10-08): NOT CLOSED.** Detailed four-point evidence: [G5_WP02_ACCEPTANCE.md](G5_WP02_ACCEPTANCE.md). Real kernel IPC endpoint + isolated Ring 3 roundtrip, atomic two-thread dispatch, opcode rights/semantic guards, bounded cancellation, and protected-state durable replay code are published and host regressions have passed. QEMU first boot persists the ledger; **second-boot recovery still fails** (PR #122/#125); PR #126 instruments second-boot serial output. Production Shell/compositor principal provisioning remains a separate critical gate. Never override this red release gate.
- [x] **WP-02 PR #113 CI evidence:** `G5 IPC Wire Contract` and `Aurora OS Bootstrap Build` completed successfully; `Identity Entropy Handoff` initially failed because the Ring 3 entropy probe was skipped (`entropy_ready()` false) but its failed-job rerun `113292813520` completed successfully. Investigate test flakiness separately; no claim of a deterministic fix or full Shell QEMU acceptance.
- [x] **WP-02 CI verification confirmed (PR #112, commit `dc4a958c`):** GitHub Actions runs `37768852838` (G5 IPC codec), `37768852981` (Aurora OS Bootstrap Build x86_64), and `37768852921` (Identity Entropy Ring3) all completed with `success`. These certify the configured workflows for that commit, **not** a full G5 Shell/QEMU functional test.
- [x] WP-02 PR-G5-001 source published: `kernel/include/aurora/g5_ipc_abi.h`, `kernel/src/ipc/g5_ipc_codec.c`, `tests/g5/ipc_codec_test.c`, `.github/workflows/g5-ipc.yml`.
- [ ] WP-02 compile/test and GitHub Actions verdict independently verified; awaiting runtime and integration evidence.
- [x] WP-02 follow-up source: `g5_ipc_decode_received()` now checks capability count (max 4), rejects zero/duplicate received handles, and preserves the rule that authoritative capability type/rights/generation validation is separate; negative tests added.
- [x] WP-02 typed opcode registry and injected fail-closed receiver-side capability validation interface; negative host test cases committed (G5 IPC source and tests).
- [x] WP-02 typed schema function added for the initial Shell/window/scene opcode shapes; independent negative test file `tests/g5/ipc_schema_test.c` committed and included in `.github/workflows/g5-ipc.yml`.
- [x] WP-02 initial host-testable dispatcher published: `g5_ipc_dispatch.h/.c`, exact-schema decode, session generation gate, opcode authorization callback and monotonic request IDs; `tests/g5/ipc_dispatch_test.c` wired to CI. This is **not** yet a real Ring 3 Shell.
- [x] WP-02 replay-safety correction: the dispatcher now reserves each authorized request ID **before** running a potentially side-effecting handler, so handler-error retries cannot re-execute within that dispatcher lifetime; updated host regression tests. This trades uncertain handler failures for non-retryable IDs. It does **not** provide crash-durable exactly-once delivery.
- [x] WP-02 kernel-only capability checker adapter implemented: `g5_ipc_kernel_cap_check()` delegates to actual `cap_lookup()` with expected type and required rights in the **receiver-owned** cap table; added host-stub contract test and CI wiring. This is **not** a live Shell integration; cap_lookup itself enforces generation/revocation in kernel.
- [x] WP-02 kernel trusted-entry wrapper `g5_ipc_dispatch_authorized()` created, requiring a valid receiver-table capability with explicit expected type and rights before invoking the existing session/opcode dispatcher. Added negative test and CI wiring. The wrapper is **not yet called by a live Ring 3 service**.
- [x] WP-02 session lifecycle bridge published: `user_session_host_register_g5_dispatcher()` and unregister; User Session Host binds registered G5 dispatcher to its Session Manager-provided generation after successful startup, and revokes it on stop/cleanup, including failure paths. **Registration is an integration hook; no live Desktop Shell endpoint is connected yet.**
- [x] Session regression CI now explicitly executes `tests/g5/ipc_session_test.c` (`33f28164`). G5 dispatcher tracks `last_revoked_generation` and denies rebind of an old/revoked generation; test updated. Source and CI configuration committed, **no confirmed executed CI result or QEMU pass**.
- [x] WP-02 static build audit: kernel Makefile discovers all `src/**/*.c`; corrected two host-test positional `g5_dispatch_context` initializers after field addition to avoid `-Wextra -Werror` missing-field warnings. Runtime compiler/CI execution **still not verified**.
- [x] WP-02 bounded pending request ledger source committed (`g5_ipc_pending.h/.c`): max 16, duplicate rejection, FULL backpressure, generation-bound removal/cancellation and reset. Regression test (`ipc_pending_test.c`) added to G5 CI and PR #114 opened for verification. The ledger is an isolated building block, **not yet connected to real IPC queues**.
- [x] WP-02 request/cancel admission helper: `g5_pending_accept_control()` decodes wire frames, validates request schema, limits pending queue to 16, removes matching requests on zero-payload CANCEL frames, rejects unknown opcodes and stale generations; test added and linked into CI. PR #115 opened to verify this final source state. Cancelling an already-running side effect is not provided.
- [x] WP-02 kernel IPC endpoint bridge and boot-validation probe (`g5_ipc_endpoint.h/.c`, `g5_ipc_endpoint_probe.c`, `kernel/src/main.c`): real `ipc_send`/`ipc_receive` queue, receiver-owned capability guard, replay-denial, session revoke check. PR #116 triggers QEMU verification; **not** an authenticated Ring 3 Desktop Shell endpoint. Exclusive sender provisioning remains a host contract, not attested by an IPC sender PID.
- [ ] WP-02 final release gates: wire actual authenticated Ring3 G5 service and capability provisioning; serialize multiple consumers/revocation with race tests; per-opcode authority/semantic validation; crash-durable replay ledger/recovery and fault injection; fresh passing QEMU end-to-end tests. These remain unverified and WP-02 must not be marked complete.
- [ ] Fresh CI verdict for latest WP-02 changes: no commit checks / PR-triggered workflow runs exposed by connector at audit time; do not claim passing.

The audit is **documentation/source review only**, not a completed G5 Core implementation/runtime gate. It identifies already present graphics syscalls and a stricter 32-input-target ceiling to preserve during integration.

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
- [x] controller Run transition with mandatory QEMU runtime marker;
- [x] connected-port discovery and reset on live qemu-xhci hardware;
- [x] command-ring producer with Enable Slot submission;
- [x] event-ring consumer with asynchronous Port Status Change draining;
- [x] validated Command Completion Event matching command pointer/completion code;
- [x] live Enable Slot completion with controller-assigned nonzero Slot ID;
- [x] context-size-aware Input Context, Slot Context and EP0 Context construction;
- [x] PMM-backed default-control Endpoint 0 transfer ring;
- [x] Address Device command submission/completion on the live command/event path;
- [x] controller-populated Device Context validation with nonzero USB address;
- [x] EP0 Running-state validation after Address Device.

Implemented and runtime-verified USB enumeration foundation:

- [x] bounded EP0 Setup/Data/Status control-IN transfer engine;
- [x] Transfer Event validation for default-control endpoint 0;
- [x] GET_DESCRIPTOR(Device) with structural validation and VID/PID/USB-version parsing;
- [x] two-stage Configuration Descriptor read (header then bounded full descriptor set);
- [x] bounded Interface/Endpoint descriptor-chain parser;
- [x] HID Boot interface classification;
- [x] interrupt-IN endpoint discovery with address/max-packet/interval extraction;
- [x] live qemu-xhci keyboard identification (HID Boot protocol 1, endpoint 0x81, 8-byte reports).

Implemented and runtime-verified live HID keyboard path:

- [x] SET_CONFIGURATION standard control request;
- [x] HID SET_PROTOCOL(Boot) class request;
- [x] xHCI interrupt-IN Endpoint Context construction;
- [x] Configure Endpoint command and Running-state validation;
- [x] dedicated interrupt-IN transfer ring;
- [x] Transfer Event completion/residual validation for HID reports;
- [x] live HID binding into the existing generational USB HID transport;
- [x] live 8-byte keyboard boot report delivery into the normalized input queue;
- [x] QEMU monitor key injection runtime gate proving key A (usage 0x04) end-to-end.

Remaining transport/extended-device work:

- [ ] enumerate/configure the second connected HID Boot device (mouse) instead of stopping after the first connected port;
- [ ] verify live mouse motion/button report delivery through xHCI into normalized input;
- [ ] live disconnect/port-change teardown and HID unbind;
- [ ] replace or formally retire the polling validation path with the final MSI-X event-delivery policy;
- [ ] touch/pen/gamepad and accessibility/input-method layers later.

Acceptance gate:

Mouse and keyboard interact with multiple surfaces without cross-client event leakage.

Current G4 gate status: **runtime verified end-to-end for a live USB HID Boot keyboard on qemu-xhci**. Aurora configures the USB device, forces HID Boot protocol, creates and enables the interrupt-IN endpoint, arms a Normal TRB, receives the hardware/emulated 8-byte report, submits it through the existing HID binding layer and verifies the resulting normalized key event. The mandatory QEMU gate injects key A and observes report usage 0x04 before the normalized-input success marker. G4 remains **In progress** because the second HID Boot device (mouse), actual port-disconnect-triggered lifecycle and the final interrupt-delivery policy still need completion. The resource teardown mechanics themselves are now runtime verified through HID unbind -> Disable Slot -> DCBAA/context/ring release.

## Phase G5 — Window protocol and Shell

Status: **In progress**

Implemented and runtime verified:

- [x] toplevel configure/ack serial protocol;
- [x] exact-serial ACK validation with stale/future ACK rejection;
- [x] configure-ready geometry validation before policy-visible transition;
- [x] one-shot, target-bound activation tokens;
- [x] bounded interaction-serial freshness for untrusted activation;
- [x] trusted Shell activation authority;
- [x] centered initial placement with bounded cascade;
- [x] Shell-owned monotonic stacking / raise policy.

Additional G5 policy hardening (implemented, runtime confirmation pending):
- bounded move with configure-ack geometry and output-bound validation;
- explicit toplevel destruction with activation-token invalidation;
- duplicate live toplevel role denial and stale surface-generation checks;
- regression selftests for stale references, denied activation and move bounds.

Remaining:

- [ ] compositor scene and input-router integration for move/resize/close;
- [x] explicit window-policy surface revocation and policy-reset APIs (code + regression selftests, runtime verification pending);
- [ ] wire the revocation APIs into real surface/process/session teardown paths;
- [ ] concurrency/lifetime auditing before use with asynchronous Ring 3 clients;
- [ ] verify new policy regression tests in QEMU and Actions;
- [ ] decorations;
- [ ] Desktop Shell process;
- [ ] launcher/panel/task switching baseline;
- [ ] move/resize/close integration with compositor nodes and input routing.

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


### G4 extended Boot Mouse controls — runtime verified

Implemented and runtime verified on the live qemu-xhci Boot Mouse path:

- [x] explicit right-button press/release decoder self-test;
- [x] explicit middle-button press/release decoder self-test;
- [x] explicit vertical wheel positive/negative decoder self-test;
- [x] live QEMU right-button injection -> xHCI interrupt-IN -> normalized pointer-button event;
- [x] live QEMU middle-button injection -> xHCI interrupt-IN -> normalized pointer-button event;
- [x] live QEMU wheel-axis injection -> xHCI interrupt-IN -> normalized scroll event.

The current Boot Mouse path therefore covers relative motion, left/right/middle buttons and vertical wheel scrolling. Back/Forward and vendor-specific extra buttons are not claimed by this gate because they generally require HID Report Protocol / Report Descriptor parsing rather than the fixed Boot Mouse report.


### G4 HID Report Descriptor foundation — runtime verified

Implemented and runtime verified on the live qemu-xhci mouse:

- [x] parse HID descriptor (0x21) and carry the subordinate Report Descriptor length;
- [x] GET_DESCRIPTOR(Report, 0x22) over EP0;
- [x] bounded HID short-item parser with fail-closed long-item/multi-report handling;
- [x] extract input report bit length, Report ID, button count and X/Y/wheel fields;
- [x] HID SET_PROTOCOL(Report) control request;
- [x] live QEMU report-layout runtime gate.

The qemu USB mouse reports **5 buttons**, a **32-bit input report**, **Report ID 0**, and a **wheel field**. This means buttons 4 and 5 are genuinely advertised by the emulated device and can be mapped to Aurora Back/Forward in the next G4 block rather than being synthesized or assumed.


### G4 five-button HID Report Mouse — runtime verified

Implemented:

- [x] generic HID mouse report-layout model shared outside the xHCI layer;
- [x] bounded arbitrary-bit extraction for descriptor-driven reports;
- [x] signed X/Y/wheel field decoding from Report Descriptor offsets and widths;
- [x] five-button transition decoding;
- [x] button 4 -> Aurora Back mapping;
- [x] button 5 -> Aurora Forward mapping;
- [x] detach/revocation releases extended buttons as well as left/right/middle;
- [x] REPORT_MOUSE transport binding with per-device descriptor layout;
- [x] descriptor-derived report-size validation and dispatch;
- [x] stale/generational transport lifecycle preserved for Report Mouse bindings.

Runtime verification:

- [x] live qemu-xhci Report Descriptor reports 5 buttons, 32 input bits, Report ID 0 and wheel;
- [x] live HID SET_PROTOCOL(Report) succeeds;
- [x] using that live descriptor layout, a conforming button-4 report decodes to AURORA_POINTER_BUTTON_BACK;
- [x] using that live descriptor layout, a conforming button-5 report decodes to AURORA_POINTER_BUTTON_FORWARD.

QEMU limitation: HMP mouse_button only exposes left/right/middle, and QEMU's current host/QMP pointer injection path does not deliver side/extra events to the emulated usb-mouse data path. Therefore Back/Forward are **descriptor-derived runtime verified**, not claimed as externally injected live USB side-button events. Real-hardware/pass-through validation remains desirable for a future hardware matrix.


### G4 USB hot-unplug lifecycle — runtime verified

Implemented and runtime verified on the live qemu-xhci mouse path:

- [x] targeted Port Status Change Event consumer for a specific root-hub port;
- [x] disconnect validation requiring PORTSC.CCS=0 and PORTSC.CSC=1;
- [x] PORTSC RW1C change-status acknowledgement;
- [x] safe draining of transfer events that may precede the disconnect Port Status Change;
- [x] QMP device_del of the live qemu USB mouse with DEVICE_DELETED confirmation;
- [x] disconnect-driven HID transport unbind;
- [x] normalized DEVICE_REMOVED verification;
- [x] disconnect-driven Disable Slot and DCBAA/context/ring release.

The final runtime gate proves a real external hot-unplug sequence from QEMU through xHCI event delivery into Aurora's input/device lifecycle.

Back/Forward support is also implemented in the descriptor-driven Report Mouse decoder and transport using the live mouse Report Descriptor (which advertises five buttons). The current Back/Forward validation is descriptor-derived inside Aurora; it is not claimed as a live xHCI side-button injection gate.


### G4 disconnect-driven HID teardown — runtime verified

Implemented and runtime verified on qemu-xhci:

- [x] targeted Port Status Change Event consumer for a known root port;
- [x] disconnect validation from live PORTSC (CCS=0, CSC=1);
- [x] RW1C acknowledge of observed port-change status;
- [x] QMP hot-unplug of the live usbmouse device with explicit DEVICE_DELETED confirmation;
- [x] live port-6 disconnect observation (PORTSC 0x000202a0);
- [x] HID transport unbind after the hardware disconnect event;
- [x] normalized DEVICE_REMOVED verification;
- [x] disconnect-driven Disable Slot + DCBAA/context/ring teardown.

The hot-unplug gate is no longer an explicit synthetic teardown path: removal begins outside Aurora through QEMU device deletion and must propagate through the xHCI Port Status Change mechanism before software and DMA ownership are revoked.

G4 completion decision:

- [x] xHCI event-delivery baseline finalized as **bounded polling for V1**;
- [x] Event Ring/ERST polling baseline validated with IMAN.IE intentionally clear;
- [x] final Event Ring quiescence verified after live hot-unplug teardown;
- [x] final G4 audit/runtime completion marker required by CI.

**G4 status: COMPLETE / runtime verified.**

MSI-X is explicitly deferred to PCI/interrupt hardening. Aurora currently detects MSI-X capability, but generic PCI MSI-X table programming and dynamic device-vector ownership are not yet platform services; they are not required for the G4 V1 input-transport completion gate.


### G4 final runtime gate

The four-CPU q35/qemu-xhci gate requires:

- `[xhci] bounded polling event-delivery baseline gate passed`
- `[xhci] live mouse Port Status Change disconnect gate passed`
- `[xhci] final polling event-ring quiescence gate passed`
- `[xhci] G4 input transport runtime completion gate passed`

Together with the earlier keyboard, mouse, Report Descriptor and teardown gates, this closes G4.
