# Aurora OS G5 — WP-01 Repository Implementation Audit
Status: **Source-reviewed architecture audit / implementation plan, not a runtime certification**
Date: **2026-10-08**
Target: `main`; companion design: [G5_DESIGN_SPEC.md](G5_DESIGN_SPEC.md), especially G5-D30 through G5-D44.

## Scope and methodology
Inspected actual current repository files via GitHub, rather than relying only on design prose: `kernel/src/graphics/window_policy.c`, `kernel/include/aurora/window_policy.h`, `kernel/src/graphics/graphics_input_router.c`, `kernel/include/aurora/graphics_input_router.h`, `kernel/include/aurora/software_compositor.h`, `kernel/include/aurora/graphics_buffer.h`, `kernel/src/arch/x86_64/syscall.c`, `kernel/include/aurora/syscall_abi.h`, `kernel/src/session/user_session_host.c`, `services/user_session/runtime/main.c`, `kernel/src/service/service_bootstrap.c`, `kernel/include/aurora/service_bootstrap.h`, `kernel/src/service/service_supervisor.c`, `kernel/Makefile` and graphics/Identity/storage docs. This is a **targeted G5 dependency audit**, not exhaustive review of every repository file. No binaries built and no QEMU tests run as part of WP-01; historical CI claims are labeled as such.

## Executive finding
G1–G4 are credible implementation foundations; G5 currently has important window policy primitives and verified graphics-input safety mechanisms, but **no complete live Ring 3 Desktop Shell → compositor → multiple application clients user workflow established by this inspection**. The first engineering priority is process/IPC/authority wiring, not new Canvas UX features or a second graphics subsystem.

## Evidence inventory
| Area | Direct source evidence | Assessment |
|---|---|---|
| Display/surfaces/renderer | `graphics_buffer.h`, `software_compositor.h`, `graphics_surface.h` and graphics roadmap | Implemented primitives; G1–G3 QEMU runtime success documented. Not a spatial desktop. |
| Graphical syscall ABI | `syscall_abi.h` defines graphics buffer map/unmap 15–16, attach/damage/commit 17–19, display present 20, frame callback request/take 21–22; handlers visible in `syscall.c` | Real Ring 3 graphics data path exists. Avoid replacing it with G5 IPC duplicate. |
| Input routing | `graphics_input_router.c/.h` offers target registration, keyboard focus, pointer capture, scene observer revocation, secure-scene filtering and queue coalescing | Substantial implementation, but its concrete router directly holds a compositor pointer; cross-process migration must be designed. |
| Window policy | `window_policy.c/.h`: 64 toplevel cap, configure/ACK, token activation, initial placement, move, raise, surface revoke and reset | Kernel-side policy foundation, not proof of a Shell-owned Ring 3 policy service. |
| IPC/capabilities | `ipc.h`, `ipc.c`, `capability.h`, `capability_abi.h` and syscall ABI: 256-byte inline payload, four transferred capabilities, 16-deep channel queue, one waiter/endpoint, capability generation/attenuation | Ready baseline for command transport; missing typed G5 protocol / discovery / generic Ring 3 bulk transport. |
| Process/session | `user_session_host.c`, `services/user_session/runtime/main.c`, `service_bootstrap.c`, `service_supervisor.c` | Real Ring 3 User Session Host, session-scoped profile lease and bounded trusted-service restart mechanisms; not yet general desktop service orchestration. |
| Storage | `docs/PROTECTED_SYSTEM_STATE.md`, Identity storage docs and D39–D43 design | Trusted small-record durability exists; general authorized, large mutable Canvas workspace journal/snapshot API not demonstrated. |
| G5 product features | `G5_DESIGN_SPEC.md` D01–D44 | Most Canvas graph, Hub module, persistence and adaptive optimization contracts are **approved designs**, not implemented modules. |

## Critical constraints verified in source
- `AURORA_COMPOSITOR_MAX_NODES=64`; existing compositor node positions are `int32_t`, scale is bounded by `AURORA_COMPOSITOR_MAX_SCALE=4`. Camera-relative Canvas projection and virtualized visible set are required.
- `AURORA_GRAPHICS_BUFFER_MAX_OBJECTS=64`, maximum buffer dimension 8192 and buffer bytes 64 MiB. Session/panel memory admission and reuse must account for these global ceilings.
- `AURORA_GRAPHICS_INPUT_MAX_TARGETS=32`; each input target has a 32-event ring. **Visible input target ceiling (32) is stricter than compositor node ceiling (64)**. Plan clear resource admission, avoid assuming 64 independently interactive windows.
- `AURORA_WINDOW_POLICY_MAX_TOPLEVELS=64`; activation tokens 32, with interaction serial age 32. These are bounded experimental capacities.
- `AURORA_SYS_IPC_PAYLOAD_MAX=256`, `AURORA_SYS_IPC_CAPS_MAX=4`; IPC has one waiter per endpoint. G5 transaction batches must use scoped handles or bounded chunk protocol with explicit atomicity.
- Existing syscall ABI contains actual Ring 3 buffer map/unmap and frame callback functions; earlier high-level design text may call generic shared-memory user ABI missing. Distinguish **existing graphics-buffer-specific mapping** from a general arbitrary shared-memory transfer API.
- Existing `aurora_trusted_service_manifest` validation requires `protected_state_scope` and nonzero protected-state rights; do **not** automatically bootstrap Shell with Identity-style protected-storage authority. Adapt/generalize the manifest or use a session-host-specific launch path, granting only Shell's session-scoped window-policy authority.
- `window_policy_create_toplevel` accepts `struct aurora_graphics_surface*` and `graphics_input_router` maintains a compositor pointer. Those internal pointers must not cross a Ring 3 IPC boundary. Introduce opaque, generation-checked cap handles at boundaries.
- Source/doc freshness: graphics roadmap marks G4 COMPLETE in later sections, while `docs/graphics/README.md` retains an older "G4 active" summary. `docs/RING3_IPC_SYSCALLS.md` contains historical notes about Identity runtime not existing that conflict with current session/service sources. Audit statuses by source + fresh CI rather than stale introductory summaries.

## Risk register — development priorities
| ID | Priority | Risk / evidence | Action and verification |
|---|---|---|---|
| G5-WP01-01 | P0 | No verified Shell/process → compositor policy end-to-end integration | Create trusted Shell launch/bootstrap, define window capability and IPC; QEMU two-process live-window gate. |
| G5-WP01-02 | P0 | Service manifest requires Protected State scope; risk overgrant | New least-privilege Shell launch manifest with no protected-state grant; CAP_CHECK negative test. |
| G5-WP01-03 | P0 | Window policy/input router use direct compositor/surface pointers | Define opaque surface/window IDs, generation checks, event-driven cross-process routing, no raw pointer IPC. |
| G5-WP01-04 | P0 | Input target max 32, compositor nodes max 64, buffers max 64 | Bounded admission and viewport virtualization, deterministic exhaustion tests. |
| G5-WP01-05 | P0 | No general atomic compositor multi-scene batch/publish ACK | MVP single-scene coherent publication policy, input gating on mismatch; fault injection and stale-hit tests. |
| G5-WP01-06 | P0 | Profile-scoped mutable durable Canvas file API not demonstrated | Build broker + verified append/sync/manifest, retention/fault recovery; mandatory pre-Core gate. |
| G5-WP01-07 | P1 now / P0 before Core | QEMU recovery test harness only designed | CI scripted cutpoints, oracle, cold reboot same disk; no success without artifacts. |
| G5-WP01-08 | P1 | No typed G5 endpoint discovery/cancellation/backpressure | Implement bounded `G5.IPC.v1`, service endpoint bootstrap, negative malformed/stale and queue pressure tests. |
| G5-WP01-09 | P1 | G5-D36 fixed-point scale and zoom transform limits provisional | Freeze C types after property test; bridge to current int32 compositor coordinates. |
| G5-WP01-10 | P1 | Documentation reports are inconsistent in status | Reconcile roadmap/README and mark each code/doc/runtime stage separately. |

## Capability/dependency matrix — requested minimum grants
| Principal | Needs | Must not receive by default |
|---|---|---|
| Desktop Shell Ring 3 | session-bound window policy/scene management, authorized app endpoint, focus and system chrome control | Identity credentials, Protected State WRITE, raw display hardware, other users' profiles |
| Compositor service | scoped surface/buffer composition and display-present access, trusted secure overlay handling | user profile content, arbitrary app filesystem, Identity authentication |
| Ordinary app process | own surface/buffer read-write + commit endpoint, own input delivery, authorized content handles | other clients' surface handles, global window placement, secure overlay role |
| State Broker | explicit user-scoped workspace read/write and durability rights | credential database, cross-user file enumeration |
| Graph/Content Broker | authorized scoped object/edge queries | implicit privileged file traversal, private data leak via counts |
| Session Manager / supervisor | mint/revoke session grants and bounded restart control | unscoped transfer of secret/profile capabilities to arbitrary apps |

Existing `AURORA_CAP_SURFACE`, `AURORA_CAP_GRAPHICS_BUFFER`, `AURORA_CAP_DISPLAY`, `AURORA_CAP_IPC_ENDPOINT` and rights READ/WRITE/MAP/CONTROL/TRANSFER should be reused. Shell-specific rights must be mapped to concrete checked existing or newly reviewed cap types, **not** to string-only `MANAGE_WINDOWS` documentation. Before code, verify mapping and ownership with a negative rights test.

## Dependency and execution order
1. **WP-01 closure:** freeze a source-path inventory, capability allocation and correction of stale status prose; collect fresh baseline build/QEMU artifacts.
2. **WP-02 Control ABI:** `g5_ipc_abi.h` serialization (D32 48-byte header), opcode table, result status, scoped cap ordinal lookup; tests for 256-byte boundary, 4 handles, stale generation and full 16-message queues.
3. **WP-03 Shell startup:** make a non-Identity privileged launch model via Session Manager/User Session Host; validate Shell READY, session lock/logout revocation and restart with fresh capabilities.
4. **WP-04 Two-client vertical slice:** Shell asks policy to create/place/move/resize/focus; compositor applies; input router routes to two independent real Ring 3 clients; destruction and crash revoke caps/input.
5. **WP-05/06 Canvas math + registry:** fixed-point camera with rebasing, reference full scan then scene tree, revisioned atomic logical commits; retain 32 interactive targets/64 nodes until safe expansion.
6. **WP-07 Visible-set projection:** bounded dirty redraw and semantic selection; upgrade compositor batch/scene ACK where correctness requires.
7. **Storage track:** in parallel prototype profile-scoped durable broker, journal/checkpoint and D43 fault oracle; integrate as mandatory P4 release gate.
8. **Core release gate:** enforce D44 success criteria with cold reboot QEMU, no privilege leak, no lost durably acknowledged edits, keyboard-only workflow, and measured regression baselines.

## Concrete first change set recommendation
Implement one narrow feature-branch PR at a time:
- **PR-G5-001 (first):** reviewed `G5.IPC.v1` wire header/codec and negative host tests; **no syscall renumbering** or new duplicate IPC primitive.
- **PR-G5-002:** Shell startup/READY and least-privilege capability grant path; serial/QEMU verification.
- **PR-G5-003:** command bridge between existing window_policy and compositor with generation/scene authority; two isolated clients.
- **PR-G5-004:** focus/input routing across process boundary and teardown tests; secure-scene regression.
- **PR-G5-005:** minimal camera+Scene Graph+reference index, virtualized viewport rendering.
- **PR-G5-006:** user-profile journal broker + cold reboot tests.
Suggested ownership: architecture/ABI reviewer, kernel IPC/session implementer, graphics compositor implementer, Shell/Canvas implementer and test/CI owner; the same developer can hold multiple roles, but review criteria remain separate.

## Acceptance status as of this audit
**WP-01 source inspection/report prepared.** No current-turn build, runtime QEMU, profiler output, fuzz campaign or new source compilation was executed. All cited QEMU-verification claims describe preexisting repository logs/docs, not fresh verification. Close WP-01 operationally only after an initial fresh CI baseline and capability grant mapping review; do not declare G5 P0/P1 implemented based on this report.
