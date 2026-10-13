# WP-05.A — Camera authority and Ring 3 synchronization gate

Status: **OPEN — NOT FROZEN**.

## Current verified path

1. The trusted kernel input router detects an eligible empty-background scroll and allocates a monotonic request ID.
2. The primary Ring 3 user-session process receives a `CANVAS_PAN_REQUEST` with expected kernel camera and scene revisions.
3. Ring 3 validates bounded nonzero deltas and stages `g5_icam_pan()` on a **temporary copy**; it does not commit local camera state.
4. Ring 3 sends `G5_OP_CANVAS_PAN` to the G5 endpoint and acknowledges **forwarding** through `CANVAS_PAN_ACK`.
5. The trusted kernel waits for that ACK, polls the G5 IPC completion, requires `G5_IPC_OK`, and verifies that its camera revision advanced exactly once while the scene revision remained stable.

## Important semantic distinction

`CANVAS_PAN_ACK` means *forwarded*, **not** *accepted by the kernel*. The Ring 3 process cannot infer acceptance from the ACK and must not update its local authoritative revision on that basis. Kernel camera revisions and integer-camera revisions are different representations until an explicit synchronization contract exists.

## Remaining implementation gate

- Define a kernel-to-Ring3 authoritative camera-state message carrying the **committed** revision and center/zoom or an equivalent deterministic snapshot. Decide whether to transmit a snapshot or an acknowledged delta; do not assume a forwarded delta was applied.
- Deliver that message only after a successful kernel commit and validate the session, process, request ID, and monotonic revision. Specify recovery for dropped/reordered messages and restart/resize.
- Reconcile the Ring 3 integer camera from the authoritative state, not from speculative requests. Cover rejection, stale revision, duplicate request, overflow, and failed compositor present.
- Add end-to-end tests and a QEMU boot/input trace before WP-05.A freeze. No graphics asset is required for this protocol stage.

## Existing guards

- Pan deltas are bounded to 32 pixels per axis; zero motion is rejected.
- Ring 3 rejects repeated or out-of-order request IDs.
- Kernel owns placement, compositor transaction, and camera revision.
- CI builds and inspects the real freestanding Ring 3 ELF.
