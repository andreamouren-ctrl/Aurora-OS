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
- Define a **Shell-owned Ring 3 camera snapshot** (center, zoom, viewport, revision), with compositor presentation acknowledgement and generation/revision fencing across resize, rejection, restart and out-of-order messages; do not confuse a committed delta with an entire camera snapshot.
- Make rendering camera-relative rather than moving world-space windows. Specify clipping, hit testing, focus, accessibility, frame pacing and offscreen culling.
- Preserve existing capability/security checks, input provenance, and IPC revision validation.
- Measure CPU, GPU, latency and memory against z-order-only switching on representative hardware.
- Run end-to-end CI/QEMU; do not freeze WP-05.A while the bootstrap smoke gate is red.

## CI status at last review

The integer-camera host workflow passed at commit `8f446985`. The QEMU BIOS four-CPU bootstrap failed at WP-05 input probe stage 6 (first USB HID wheel-to-pan) on that commit. Commit `d3d9b498` added detailed diagnostics; results are not yet established in this document.

## Architecture correction — Shell-owned camera (2026-10-13)

The accepted `docs/KERNEL_MODEL.md`, `docs/adr/ADR-0006-graphics-compositor-split.md`, `docs/graphics/DESKTOP_SHELL_CONTRACT.md`, and `G5_DESIGN_SPEC.md` require the final spatial camera to be owned by the trusted **Ring 3 Desktop Shell / Canvas Engine**, not by Ring 0. Canvas Engine may initially be an in-process Shell module; it does not require a dedicated always-awake process.

- **Ring 3 Shell/Canvas:** sole mutable camera center, zoom, viewport, scene/world positions, input-context policy and task switching. World rectangles are immutable under camera pan/zoom.
- **Compositor:** consumes a generation- and revision-fenced projected scene transaction, performs clipping, transforms, hit-test correspondence and atomic presentation. It must not independently advance a second camera.
- **Kernel:** enforces capabilities, IPC endpoint identity, session/object generations and trusted input provenance; no persistent camera/navigation policy in Ring 0. Existing kernel compositor/window-policy code is a temporary bring-up bridge, not the final ownership model.
- **Acknowledgements:** include complete accepted camera state (center, zoom, viewport, revision) or a reference to the exact submitted snapshot. A revision-only rejection/commit cannot reconcile divergent camera state. `g5_icam_accept_snapshot` is a pure Ring 3 staging primitive; it does not authenticate the sender and must only be called after IPC authorization.
- **Atomicity:** Shell stages a camera and visible scene, compositor validates all authorized surfaces before presentation, and Shell publishes its new committed camera only on the matching accepted transaction. Failure leaves the previous camera and frame intact.
- **Performance:** event-driven input, bounded coalesced scene submissions per frame, no per-pixel IPC, no unconditional background polling. Measure frame latency and wakeups before claiming improvements.

### Migration sequence and current limitation

1. Introduce complete Ring 3 snapshot acceptance and host negative tests (first code slice; **not yet wired into production IPC**).
2. Add typed camera/scene transaction IPC with generation and monotonic revision fencing; make Ring 3 the owner of the committed camera and compositor the validator/presenter.
3. Replace kernel-owned pan/scroll policy and physical window translation with immutable world coordinates, camera-relative projection and consistent input hit testing. Keep the existing QEMU pilot as a regression gate until replacement is proven.
4. Implement contextual wheel zoom, keyboard/drag pan, right-held-wheel application switching, fractional composition, and compositor crash/restart/revocation tests.
5. Verify QEMU first/cold boot and p50/p95 latency, CPU wakeups and memory; only then propose WP-05 freeze.

**This section records the target architecture, not a claim that Ring 0 graphics policy has already been migrated.**

### Ring 3 compositor submission primitive (2026-10-13)

`g5_icam_prepare_submission` stages a bounded set of world-space rectangles into one complete immutable projected frame, tagged with session generation, scene revision, camera revision and request ID. `g5_icam_match_presented` requires matching identity/revisions plus an advancing display serial. `g5_icam_hit_projected_scene` uses the same clipped projected frame for focus as presentation. Host tests include atomic rejection, offscreen culling, overlapping focus, stale revisions and replayed ACKs.

**Boundary:** these are pure Ring 3 data/validation helpers. They are not a new IPC wire format, do not authenticate the compositor, and do not yet send transactions to the production compositor. The kernel's old two-window physical translation remains a bring-up compatibility path until the compositor and input router can consume the new scene submission together. Never treat a helper-level ACK match as proof of authorization without a trusted IPC sender and verified capabilities.

## IPC integration gate — bounded transport (2026-10-13)

Source audit: `kernel/include/aurora/g5_ipc_abi.h`, `services/user_session/runtime/main.c`, `kernel/src/session/g5_shell_scene.c`.

- The existing G5 message envelope is 48 bytes, with at most `AURORA_SYS_IPC_PAYLOAD_MAX - 48` bytes of inline body. The existing `G5_OP_CANVAS_PAN` request carries only 24 bytes: signed x/y pixel deltas and expected camera/scene revisions.
- The kernel's `g5_shell_scene_pan` currently validates an exclusive Shell sender and physically translates two window placements. This must remain a **compatibility path**, not become the new infinite-canvas rendering protocol.
- A full `g5_icam_scene_submission` (up to 16 projected objects) **must not be sent as a native C struct in one G5 inline message**: it exceeds the inline envelope, is not a stable LE wire format, and raw surface IDs are not capabilities.
- Integration sequence: (1) define a versioned bounded little-endian scene-batch wire schema with explicit count, generation, camera/scene revision, and request ID; (2) implement receiver-side surface capability and generation checks; (3) stage scene and transform in compositor; (4) atomically present and return an authenticated display serial; (5) only then publish the camera in Shell and use that same presented scene for hit-testing.
- For larger scenes, design capability-validated shared scene buffers or chunked batches with an atomic commit barrier. Never partially display a batch or silently truncate it to fit IPC. Preserve the legacy boot/QEMU path until the new transport passes end-to-end tests.

**Gate is OPEN.** The existing Ring 3 projection, submission, ACK matching, and hit-test helpers do not yet constitute an actual compositor IPC implementation. CI runs observed for the current PR remain queued; no passing result is claimed here.

## Integration boundary audit (2026-10-13)

The currently green host camera tests do not establish a production scene IPC route. Before enabling one, update the G5 opcode schema and receiver authorization together: the existing `G5_OP_SCENE_PREPARE`/`G5_OP_SCENE_PUBLISH` are control-plane operations, and `G5_OP_CANVAS_PAN` is the legacy 24-byte kernel-owned pilot. Never silently reinterpret any of those payloads as the new 56+48*N camera batch.

A valid camera batch occupies 104, 152, or 200 bytes of G5 inline payload for 1–3 items. The receiver must verify the *authenticated* envelope session generation/request ID and enforce capability type, rights, ownership, live object generation and per-surface authorization before staging any surface. An inline numeric `surface_id` is not a capability. Reject a frame with more than three items until an atomic multi-batch/shared-buffer transaction is implemented; never display the first three and silently drop the rest.

The compositor must acknowledge a specific submitted camera snapshot and scene revision with a monotonic display serial; Shell must not commit a speculative camera until a matching authenticated presentation response arrives. Failed validation, timeout, restart, or rejection must leave the last committed camera and presented scene unchanged. Replace the legacy physical window-translation path only after QEMU exercises the new route end-to-end, including replay/reorder and restart cases.

**Gate remains OPEN:** this is an implementation contract, not evidence that the transport is connected.

### Build isolation regression and follow-up (2026-10-13)

A direct include of `aurora/g5_ipc_abi.h` in the standalone Canvas camera header broke the host test compile: the camera gate intentionally supplies only `-Iservices/canvas/include`. The include was reverted. The camera header now mirrors the current 48-byte envelope / 256-byte message bound and statically verifies the 200-byte batch fits the resulting 208-byte inline capacity. **Before production IPC wiring**, add an integration-only compile-time equivalence check against `G5_IPC_WIRE_INLINE_MAX` in a translation unit that already has both include roots; do not force a kernel include dependency into the standalone arithmetic module. This is a pending integration requirement, not a claim of runtime interoperability.

### Verified production dispatcher boundary (2026-10-13)

Source inspection confirms that `kernel/src/session/user_session_host.c` routes `G5_OP_SCENE_PUBLISH` to `g5_shell_scene_publish`, and `G5_OP_CANVAS_PAN` to `g5_shell_scene_pan` in `kernel/src/session/g5_shell_scene.c`. The latter requires two active surfaces, a bounded 24-byte pixel delta and matching scene/camera revisions, then moves the two legacy window-policy toplevels. **Neither handler consumes the Ring 3 `G5SB` camera batch.** The existing `SCENE_PUBLISH` payload is a 16-byte object ID plus commit serial, not the 56–200-byte camera batch. Therefore the future camera-batch IPC must have a distinct explicitly authorized schema/operation and receiver with capability-checked surfaces, generation fencing and atomic compositor presentation; do not repurpose the current opcodes or claim that the current legacy path provides infinite-canvas semantics.


### Production dispatch and authority gate (2026-10-13)

An additional source-level audit of `kernel/src/session/user_session_host.c`
confirms that `production_g5_authorize` only allows the legacy scene
publish/place/close/pan operations for an active scene. Its handler has **no**
`G5_OP_CANVAS_SCENE_BATCH` branch. This is intentional fail-closed behavior:
the newly validated wire message must not be promoted into an unguarded
kernel scene mutation.

The integration boundary is therefore explicit:

1. The Shell/Canvas Engine in Ring 3 prepares and owns camera snapshots,
   world-space geometry, projected clips and focus resolution.
2. A trusted receiving service resolves every submitted numeric surface ID
   against live capability records, including the authenticated session,
   owner, rights, object generation and revocation status. Numeric IDs alone
   must never authorize display.
3. The compositor stages **all** members of the accepted frame, rejects
   incomplete or oversized submissions, and publishes only on an atomic
   present. A 3-item wire limit is not a 3-item infinite-canvas design limit.
4. The receiver returns a presentation ACK bound to the authenticated request,
   session, camera revision and scene revision, with a strictly increasing
   display serial. Shell commits its camera only on that ACK.
5. Tests must cover a forged/revoked surface, stale generation, duplicate
   surfaces, replayed request, compositor failure and restart, as well as
   positive end-to-end QEMU presentation.

The current IPC codec guards envelope and geometry but does not perform
steps 2–4. Do **not** authorize the opcode in the production dispatcher until
the receiver and its tests exist. This gate remains OPEN.


### Compositor source-crop implementation gate (2026-10-13)

The concrete compositor API inspection identifies a missing primitive:
`software_compositor_set_node` controls position, z, opacity and visibility;
`software_compositor_set_transform` supports integer scales 1–4 and
quarter-turn rotations, but neither can express a source rectangle. The
`destination_to_source` sampling path currently maps the *entire* committed
buffer. Passing G5SB crop fields directly to these setters would render
unintended pixels and would not implement camera zoom.

Required compositor changes before enabling `G5_OP_CANVAS_SCENE_BATCH`:

1. Introduce an explicitly validated per-node source crop, with positive
   dimensions and overflow-safe bounds against the **committed buffer**
   dimensions. Keep existing uncropped callers backward compatible.
2. Make sampling, occlusion, damage tracking and hit-testing use the same
   effective cropped geometry; a hidden or zero-intersection node must
   neither paint nor receive pointer events.
3. Implement a trusted multi-node staging/commit API with an all-or-nothing
   presentation boundary. Validate every surface's live capability, owner,
   object generation, committed buffer, configuration and scene membership
   *before* mutating any node; roll back all staged state if presentation
   fails.
4. Bind a monotonic display serial to the authenticated request ID,
   session generation, scene revision and camera revision. Shell commits
   its speculative camera only after a matching ACK.
5. Exercise positive and negative host tests and QEMU end-to-end before
   admitting the opcode through `production_g5_authorize`.

**Important:** G5SB `projected.width_px/height_px` may differ from the
committed buffer dimensions because the Ring 3 camera applies zoom.
A crop of the *projected* rectangle is not automatically a crop of source
pixels. The renderer must define and test the scale/sampling conversion
explicitly; never silently treat projected source offsets as buffer offsets.
The present gate remains OPEN.
