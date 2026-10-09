# Aurora M4 Graphics Implementation Roadmap

Status: **Canonical implementation plan**
Version: **0.2**

This roadmap orders M4 work by hard dependencies. A checkbox in the global roadmap is completed only after the relevant implementation and runtime gate are satisfied.

## G5 WP-03 — Frozen verified baseline (2026-10-09)

**Status: FROZEN / ACCEPTED within the WP-03 Shell bootstrap and lifecycle scope.** GitHub Actions commit `096464c83aa76b3b4c91ddd4fddf5c1676f61c6f` completed four of four workflows successfully: Aurora OS Bootstrap Build, G5 IPC QEMU Cold Boot Recovery, Aurora Identity Entropy Handoff, and Aurora Entropy Source Policy. Bootstrap/Identity enforce the dedicated Ring3 Shell crash-and-reauthentication lifecycle marker, in addition to the real Ring3 G5 IPC-to-compositor/display marker. No test gate was weakened to declare acceptance.

**Freeze policy:** preserve this commit as the last verified WP-03 baseline. Any further edits on the PR are outside the frozen baseline until independently revalidated; security-critical fixes can reopen WP-03 with explicit regression evidence. Work on Identity takes priority. **WP-04 has not started:** dynamic multi-client window policy, focus/hit-testing, window resize/close and the interactive Living Canvas remain future milestones. This freeze does not mean all of G5 or Identity is complete.

## G5 WP-03 — Formal closeout candidate (2026-10-09)

**Scope:** WP-03 as formally defined by `G5_DESIGN_SPEC.md` is the Ring 3 Shell bootstrap and session lifecycle, including clean stop/lock, fresh instance creation, crash, revocation, and reauthentication. Dynamic multi-client window management, resize/close/focus/input, and the complete Canvas UI begin at **WP-04 and beyond**.

**Verified previous baseline:** GitHub Actions commit `a21b5b4f505dc2b9d4b5470e6efb3a20c353b69a` completed all four workflows successfully. Bootstrap and Identity QEMU logs show the real Ring3 User Session Host, G5 scene IPC/frame presentation, and completed lifecycle probe without diagnostic failures. These are CI-run test results, not evidence of a completed desktop UI.

**Final explicit-gate revision:** the self-test emits `[g5-wp03] Ring3 Shell crash and reauthentication lifecycle gate passed` only after checking initial authorized session, repeated health traffic, anti-replay, generation and object checks, clean stop/revocations, fresh generation bootstrap, malformed-control induced Ring3 crash, and stale capability/compositor teardown. BIOS Bootstrap and Identity CI now require that exact marker. Treat WP-03 as **accepted only if the new head has all required workflows green**. Avoid weakening those markers or conflating WP-03 with WP-04.

## G5 WP-03 — Shell lifecycle: clean stop, lock/unlock, crash and reauthentication (2026-10-09)

- [ ] **Formal WP-03 scope:** `G5_DESIGN_SPEC.md` assigns Shell bootstrap, Ring 3 lifecycle, session start/lock/restart and crash + reauthentication QEMU gate to WP-03. The multi-client compositor/window management, focus and runtime resize/close contract is assigned to **WP-04**, and must not be falsely made a prerequisite for WP-03 closure.
- [ ] **Separate trusted Shell incarnation (CI pending):** Session Manager's authenticated generation may remain unchanged over a lock/unlock. Each new Shell process now gets a strictly increasing receiver-local G5 generation, authenticated against the live Session Manager generation; revoked Shell generations are never reused. Its endpoint rights remain exclusive and non-transferable.
- [ ] **Forced crash + fresh authentication test (CI pending):** native QEMU User Session Host probe performs clean generation-1 stop/revocation, then generation-2 bootstrap with new endpoints and scene, health round-trip, malformed-control induced nonzero Ring 3 exit, and verified fail-closed process/endpoint/compositor cleanup. No accepted status until fresh QEMU gate passes.

## G5 WP-03 — Native graphics IPC authority binding (2026-10-09)

- [ ] **G5 `SCENE_PUBLISH` policy mask fix (CI pending):** QEMU diagnostics confirmed the ABI v3 startup fix reaches G5 READY/HEALTH, then rejects the scene before any handler was called. Code review of `g5_ipc_endpoint_poll` proved the receiver binding explicitly checks `(authority_rights & opcode_required_rights)==opcode_required_rights`; the User Session Host had granted kernel SYSTEM READ|CONTROL|WRITE but mistakenly advertised only READ in the binding. Correct the binding to all three rights while retaining the Ring3 sender's exclusive WRITE-only and no-transfer capability. Operation dispatch policy and per-session generation still gate every command.

## G5 WP-03 — Ring3 ABI v3 entrypoint diagnosis and fix (2026-10-09)

- [ ] **Verified failure root and corrective code (CI pending):** the initial Ring3 graphics ABI v3 integration consistently failed the User Session Host native QEMU self-test before CONTROL READY. Inspection found `services/user_session/runtime/entry.S` still copying the old 64-byte startup while C required 112 bytes; the entrypoint now copies all 14 8-byte fields into protected call-stack storage with corrected initial-RSP math. This defect is distinct from earlier QEMU M1 timeout failures.
- [ ] **Prevent recurrence (CI pending):** new `scripts/check-user-session-entry-abi.py` validates ABI size versus assembler source/destination offsets and call stack frame; it is a prerequisite to compiling `entry.S`. Native Ring3 QEMU acceptance is mandatory before marking the fix complete.

## G5 WP-03 — Authenticated Shell Ring3-to-Display Slice (2026-10-09)

- [ ] **Production and QEMU Shell frame flow (new CI pending):** User Session Host ABI v3 passes non-transferable Ring3 GRAPHICS_BUFFER and SURFACE handles plus trusted object identity; its real Ring3 process maps and paints 160×96 pixels, attaches/damages/commits the surface through graphics syscalls, and sends an authorized G5 `SCENE_PUBLISH` request after READY/HEALTH. Kernel checks session, capability rights, object identity/generation and committed serial and routes the frame through Surface Registry, Configure/ACK, Frame Submission, Frame Delivery, Compositor Bridge and actual display. It then sends ABI-valid `WINDOW_PLACE` to reposition that same authorized surface, requiring a second increasing display serial and verifying ownership again. Session revoke removes scene nodes and graphics capabilities. Both production login and boot-validation host probe require successful G5 Shell scene presentation before declaring readiness; Bootstrap CI now requires a dedicated real-QEMU marker. No CI success claim before the new revision passes.
- [ ] **Remaining independent-Shell/desktop functional breadth:** this proves a minimal Shell frame through real Ring3 IPC, not yet full window manager: dynamic multiwindow creation/configure/close, focus/input, Shell crash supervision/restart with new capabilities, and final QEMU screen-content assertion must be completed before final WP-03 acceptance. No G6 work until then.

## G5 WP-03 — Real Ring3 Surface-to-Display Integration Gate (2026-10-09)

- [ ] **Real boot-validation display stage (CI pending):** extend the existing native two-client Ring3 graphics syscall probe to take two genuinely committed process-owned surfaces through a capability-backed G5 Surface Registry, Configure/ACK, Frame Submission, Frame Delivery, G5 Compositor Bridge and `software_compositor_compose_present()`; it requires nonzero display serial, detaches nodes and revokes every binding. Emit a unique `[g5-graphics]` marker only after success, now mandatory for Bootstrap BIOS acceptance. This proves the G2 Ring3 surface→compositor→display chain in QEMU, **not yet the missing G5 Shell IPC→surface chain**. No stubs or host mocks in this gate.
- [ ] **WP-03 still blocked:** an authenticated Shell G5 opcode must create/control/present an owned surface in production; the live session graphics coordinator, Ring3-created window semantics and crash/restart display acceptance remain open.

## G5 WP-03 — BIOS runtime completion and CI timing (2026-10-09)

- [ ] **Bootstrap stability investigation (new CI pending):** HEAD `8ccb3ffa` built the kernel/ISO and emitted the actual native G5 receiver runtime marker, but four-vCPU QEMU BIOS smoke did not emit the final M1 user-space bootstrap marker within its 25-second run. No demonstrated kernel panic in its serial tail; do not attribute to Shell IPC without proof.
- [ ] **Non-bypass timing adjustment:** increase the four-vCPU QEMU run budget to 45 seconds and the HID injection wait loop to 45 seconds. All existing required success markers, device input gates, errors and failure handling remain mandatory. Acceptance requires a fresh green Bootstrap run.

## G5 WP-03 — Production Login IPC Binding (2026-10-09)

- [ ] **Production dispatcher and acceptance gate (new CI pending):** actual login now registers a default least-privilege G5 dispatcher for READY/HEALTH tied to the Session Manager's active generation, provisions the exclusive Ring3 sender and kernel receiver via the already-existing User Session Host flow, and refuses to enter the authenticated session UI if the post-bootstrap Ring3→G5 health request fails. The User Session Host tears down an active process if its production handshake effect count disagrees. This is a real production integration rather than a validation-only registration; it is **not** the complete window/surface graphics protocol.
- [ ] **Remaining WP-03 exit criteria:** implement a graphical Shell dispatcher/service, capability-backed window/surface lifecycle and verified compositor-to-display frame under a genuine Ring3 request; prove cancellation, crash/restart and session revoke with QEMU artifacts before closing.

## G5 WP-03 — Persistent Ring 3 control-loop health (2026-10-09)

- [ ] **Repeated live control-loop and post-stop denial (CI pending):** host self-test requires two separate authenticated Ring3 `HEALTH_POLL`/`SHELL_HEALTH`/`HEALTH_ACK` cycles with monotonically increasing request IDs, checks exactly three health effects including bootstrap, then proves that the same API denies requests after stop with no further effects. This remains control-plane acceptance, not graphical end-to-end.
- [ ] **Live control-loop round-trip (CI pending):** User Session Host Ring3 handles `HEALTH_POLL` after its original startup handshake, sends a strictly increasing session-generation-bound `SHELL_HEALTH` over the exclusive G5 IPC sender, and acknowledges the control request. Kernel `user_session_host_health_check()` verifies both responses. QEMU integration self-test requires a second HEALTH effect after rejection of duplicate and wrong-generation traffic; stop must revoke capabilities. This is an ongoing control loop, **not** the full desktop Shell or graphics frame-to-display path.
- [ ] **Production Shell/displays still missing:** compositor surface allocation and rendering triggered by Ring3 IPC, real display confirmation, independently supervised Shell principal and crash/restart end-to-end gate. WP-03 cannot be marked accepted yet.

## G5 WP-03 — Live session Ring 3 negative IPC integration (2026-10-09)

- [ ] **Authenticated live-session receiver regression (new CI pending):** after the real Ring 3 User Session Host sends G5 READY/HEALTH, the host kernel integration test submits an already-used HEALTH request ID and a fresh HEALTH request with the wrong session generation over the same live native IPC channel. Both must be denied without incrementing the effect counter, then session stop must revoke both endpoint rights. This checks a running authenticated session, but is not a production graphical Shell or compositor/display end-to-end.
- [ ] **Outstanding WP-03 exit gate:** persistent independently supervised Shell service with ongoing Ring 3 requests, surface lifecycle and actual display presentation QEMU evidence, including restart/crash recovery. Keep unchecked.

## G5 WP-03 — Ring 3 Shell IPC startup progression (2026-10-09)

- [ ] **READY rejection and dispatcher registration isolation (CI pending):** rejecting the first authenticated Ring3 `SHELL_READY` now immediately revokes both G5 capability ends in addition to the dispatcher; registration rejects an unreaped host even when `host.active` is false. Current QEMU validation pending. These are failure-path guards, not compositor integration.



- [ ] **Ring 3 two-message bootstrap (CI pending):** the actual User Session Host sends G5 `SHELL_READY` (event ID 1) and `SHELL_HEALTH` (request ID 2), both bound to its trusted session generation and exclusive sender capability, before notifying the kernel control READY. The kernel G5 endpoint dispatches and validates both; the host self-test requires one callback for each. This exercises a genuine Ring3→syscall→kernel receiver flow; not yet a long-lived Desktop Shell Coordinator or compositor presentation.
- [ ] **WP-03 exit criteria still open:** persistent Shell receive/dispatch loop, real window/surface operations, display output proof, and crash/restart QEMU validation. Keep WP-03 open regardless of this startup smoke passing.

## G5 WP-03 — Revocation and Bootstrap diagnosis (2026-10-09)

- [ ] **New development PR (pending CI):** kernel QEMU native Shell receiver probe extended to queue a request and assert denial after receiver-side authority capability revocation, then endpoint capability revocation. Effects counter remains exactly one. These controls are kernel-thread integration tests, **not a Ring 3 Shell service**; full CI/QEMU verdict pending.
- [ ] **Bootstrap diagnostic improvement (pending CI):** when the M1 user-space completion marker is absent, print the last 60 serial-log lines before the interrupt trace. This does not bypass or relax the required markers.
- [ ] **Historical failure diagnosis:** PR #160 run `37869625627` compiled kernel/ISO and printed `[g5-shell] native IPC receiver/replay/revoke probe passed`, but BIOS smoke boot returned exit code 1 because the final M1 user-space bootstrap marker was not found. This is a later boot-completion failure, **not evidence that the Shell IPC probe failed**.
- [ ] **User Session Host teardown hardening (pending CI):** explicitly revoke G5 receiver endpoint and authority capabilities, disable the binding and clear readiness on stop/cleanup. The existing authenticated Ring 3 host self-test now asserts these post-stop invariants. Not a production Shell-to-display service.
- [ ] **Failed Ring3 host bootstrap hardening (pending CI):** READY handshake failure revokes both G5 capabilities and dispatcher before unwind; a still-live failed host is not silently overwritten by a fresh startup. A later retry may reap a finished failed thread first. Requires runtime QEMU acceptance.
- [ ] **Next production milestone:** provision a genuine Ring 3 Shell principal and endpoint under the authenticated User Session Host; run a bounded receive/dispatch loop, connect authorized surface operations to compositor presentation, and validate display output plus crash/restart revocation in QEMU. No end-to-end acceptance yet.

## G5 WP-01 — Source audit baseline (2026-10-08)

- [x] Targeted current-source inspection and P0/P1 risk register: [G5_WP01_REPOSITORY_AUDIT.md](G5_WP01_REPOSITORY_AUDIT.md).
- [ ] Fresh baseline build + QEMU CI artifact validation (not executed by this documentation audit).
- [ ] Review/freeze actual Shell capability mapping and least-privilege launch path.

- [x] **WP-02 control-plane acceptance (2026-10-08):** PR #139 passed Entropy Source Policy, Identity Entropy Handoff, G5 QEMU Cold Boot Recovery and Aurora OS Bootstrap Build. Exclusive Ring 3 User Session Host sender capability, Session Manager generation binding, logout revocation, bounded G5 IPC and persistent replay tested. This accepts the **WP-02 bootstrap/control-plane baseline**, not production Desktop Shell/compositor or exactly-once effects. Historical unchecked WP-02 entries below are superseded by [WP-02 acceptance evidence](G5_WP02_ACCEPTANCE.md).
- [ ] **WP-03 QEMU Shell IPC negative acceptance (PR #161):** real IPC-channel probe rejects mismatched session generations and ABI-valid but unauthorized WINDOW_PLACE operations; Bootstrap CI requires the native receiver/replay/revoke runtime marker. The PR #160 probe itself emitted its success marker, but Bootstrap overall failed later, so full job acceptance was NOT achieved. PR #161 awaits five-workflow CI and further boot diagnosis. This remains kernel-thread IPC testing, not Ring3 Shell-to-display.
- [ ] **WP-03 QEMU runtime Shell receiver validation (PR #160):** the actual Aurora IPC-channel probe is now invoked on a dedicated scheduler kernel thread during `AURORA_BOOT_VALIDATION`; it exercises real send/receive, duplicate replay rejection and fail-closed post-revoke denial. Kernel panic on unsuccessful probe and success log `[g5-shell] native IPC receiver/replay/revoke probe passed`. PR #159 all five workflows green but probe only built; #160 QEMU runtime verification pending. This does **not** yet prove Ring 3 Shell service or compositor display.
- [ ] **WP-03 real native IPC-channel receiver probe (PR #159):** new `g5_shell_receiver_native_self_test()` sends a framed request through Aurora `ipc_send`/`ipc_receive` and checks allowed effects, replay denial, and revoked-session rejection. The probe is currently compiled but **not yet invoked by the QEMU boot harness**; passing build CI alone is not runtime acceptance. A trusted nonzero consumer-thread boot test harness and Ring3 Shell-to-compositor delivery remain open.
- [ ] **WP-03 native Shell receiver IPC binding (PR #158):** capability-provisioned native IPC endpoint, exclusive consumer thread and fail-closed connection rules; active session-generation-gated polling through the existing `g5_ipc_endpoint_poll` infrastructure; explicit disconnect on revoke. Added negative stub-backed host regression. PR #157 G5 IPC, Entropy, Identity green; Bootstrap failure in boot log with no explicit panic, QEMU pending at prior check. PR #158 CI pending. No live Ring3 Shell compositor-to-display proof.
- [ ] **WP-03 Shell receiver dispatcher batch (PR #157):** compose `g5_shell_receiver` using the existing G5 IPC dispatcher and receiver-local opcode policy; operation callbacks run only for authorized live session generations; explicit revoke and fresh-generation bind suppress stale authority. `tests/g5/shell_receiver_test.c` runs sanitizer-backed host checks. Acceptance requires full #157 CI. This is not yet a real Ring 3 Shell service or IPC-to-display probe.
- [ ] **WP-03 Shell receiver policy batch (PR #156):** service-local opcode permission bitmap, live session-generation authority gating, and fail-closed operation callbacks/revocation. `g5_shell_policy.h/.c` and `tests/g5/shell_policy_test.c` added to CI. PR #155 accepted with five green workflows. PR #156 CI pending. Production Shell Coordinator receiver wiring and Ring 3 IPC-to-display remain open.
- [ ] **WP-03 live User Session Host lifecycle integration (PR #155):** `g5_shell_session` is now bound to the actual Ring 3 User Session Host lifecycle: activation after G5 READY handshake, immediate generation revoke at stop, and fail-closed cleanup path. Host regression `tests/g5/shell_session_test.c`. Still no production Shell Coordinator message loop, compositor IPC or frame-to-display end-to-end gate. CI pending.
- [ ] **WP-03 compositor node identity fencing (PR #154):** cache object-id and object-generation on attach; reject frame presentation if registry slot now refers to another surface; erase identity fences only after node removal succeeds. Negative regression in `tests/g5/compositor_bridge_test.c`. Prior #153 G5 IPC and Entropy were green, other CI checks still being observed; #154 full CI pending. Still no end-to-end Ring 3 G5 compositor proof.
- [ ] **WP-03 triple-integration node lifecycle (PR #152):** per-frame graphics-surface capability and generation revalidation, reject destroyed/stale surfaces with zero display serial on failure, and explicit compositor node detach with retryable cleanup after removal failure; negative regression `tests/g5/compositor_bridge_test.c`. PR #151 finished all five workflows green and supersedes failed intermediate PR #150 checks; PR #152 full CI pending. This remains kernel-side contract wiring, not production Ring3 Shell IPC-to-display acceptance.
- [ ] **WP-03 compositor failure rollback (PR #150):** delivery receipt cancellation API, `g5_compositor_bridge_present()` rollback with zeroed output serial when actual `software_compositor_compose_present()` fails, and fault-injection host regression proving committed snapshot release and no leaked receipt. PR #148 G5 host initially failed on strict compiler indentation warnings, fixed in commit `b0af5c3`; follow-up PR #149 G5 host passed. Fresh #150 full CI pending; still no live Ring 3 compositor service.
- [ ] **WP-03 triple-integration batch #7 (PR #148):** first actual `software_compositor` API bridge in `g5_compositor_bridge`: capability/Configure-ACK-gated scene-node attach, validated frame delivery and `software_compositor_compose_present`, plus compositor-node teardown when the session is revoked. Combined host test `tests/g5/compositor_bridge_test.c` and full CI. Bridge is currently trusted in-kernel scaffolding; **no production Ring 3 compositor integration**. Earlier PR #147 returned 4/5 green with Bootstrap failure, requiring a fresh full green result.
- [ ] **WP-03 triple-integration batch #6 (PR #147):** immutable delivery session epoch at bind; `publish`/`ACK` guarded by matching active presentation queue and session epoch; teardown/restart rejection of stale delivery bindings. Expanded regression in `tests/g5/frame_delivery_test.c`. Prior PR #146 had 4/5 checks green and Bootstrap failure with no explicit kernel panic found; retain as CI issue pending full fresh acceptance.
- [ ] **WP-03 triple-integration batch #5 (PR #146):** bounded eight-slot frame-delivery receipts and backpressure; session-scoped presentation-serial ACK and replay rejection; explicit revoke of outstanding receipts. Combined host regression `tests/g5/frame_delivery_test.c` linked into G5 IPC CI. This remains a service-local component, not yet a running Ring 3 compositor. Await full CI verdict.
- [ ] **WP-03 triple-integration batch #4 (PR #145):** service-local `g5_frame_submission` links the Surface Registry, Configure/ACK gate and bounded Presentation Queue in one serialized protocol; three parts are ACK-gated request admission, compositor-facing snapshot/presentation completion with monotonic serial and anti-replay, and cancellation/revocation on session teardown. Combined regression `tests/g5/frame_submission_test.c`. This is **not yet a running Ring 3 compositor**; CI pending.
- [ ] **WP-03 triple-integration batch #3 (PR #144):** presentation admission queue with capacity 16/backpressure, monotonic replay-resistant request IDs bound to the active session, and explicit cancel/revoke semantics. Host tests include stale generations, wrong configure serial, duplicate completion and queue exhaustion. This is a standalone request-control component, not yet a compositor worker or frame scheduler; CI pending.
- [ ] **WP-03 triple-integration batch #2 (PR #143):** bounded service-local surface registry (capacity 16 and duplicate rejection), monotonic presentation serial after Configure/ACK and committed snapshot validation, session-wide teardown sweep with stale-generation denial. Combined test `tests/g5/surface_registry_test.c` and GitHub CI. This registry is an in-kernel contract prototype and is not yet a production Ring 3 Shell coordinator.
- [ ] **WP-03 triple-integration batch #1 (PR #142):** capability-authenticated graphics-surface binding (`g5_surface_bridge`), Configure/ACK-gated committed snapshot reads, and fail-closed lifecycle revocation on stale session/surface generation or destroyed surface; combined negative host test `tests/g5/surface_bridge_test.c`. No claim yet of a production Ring 3 compositor or full Shell. CI in progress.
- [ ] **WP-03 Surface Configure/ACK baseline:** source `g5_surface_configure.h/.c`, session-fenced object generations, strictly increasing configure serials, latest-configure-only ACK, bounded geometry and revision conflict detection; regression in `tests/g5/surface_configure_test.c`. PR #141 verifies this independent state machine. This is **not yet connected** to a live compositor, service ownership registry, or graphics buffers.
- [ ] **WP-03 Session/Scene baseline — in progress:** Implement G5.SessionContext.v1 generation fencing, revision compare-and-swap and revocation; host regression in `tests/g5/session_context_test.c` and CI PR #140. This is a standalone contract, not yet wired into compositor scenes.
- [ ] **WP-03 subsequent milestones:** session-owned Shell Coordinator service boundary and supervisor integration; surface ownership & configure/ack; compositor frame submission and cancellation; fault/restart and stale-session QEMU proofs. No full Shell readiness claim until these pass.
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
