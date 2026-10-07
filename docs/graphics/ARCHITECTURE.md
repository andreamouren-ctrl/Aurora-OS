# Aurora Graphics Architecture

Status: **Canonical architecture contract**
Version: **0.1**

## 1. Goal

Aurora M4 introduces a graphics stack that can evolve from a simple software compositor to accelerated rendering without changing application-facing ownership or security contracts.

## 2. Components

### Display Service

Owns physical display outputs, modes, scanout targets, presentation timing and backend-specific display state. It does not own desktop policy.

### Aurora Compositor

Owns composition of authorized surfaces into output frames. It tracks scene state, clipping, transforms, visibility, damage and presentation.

### Window Manager / Shell policy

Owns user-facing placement, stacking policy, focus policy, workspaces/Activity Spaces, decorations and system chrome. In the initial implementation this policy may live in the Desktop Shell process, but it remains logically separate from composition.

### Desktop Shell

Trusted user-session component that implements the Aurora desktop experience: panels, launcher, task switching, Activity Spaces, desktop surfaces, notifications, system overlays and customization.

### Clients

Applications and system apps render into their own buffers and submit those buffers to compositor-managed surfaces.

## 3. Process model

Target production split:

```text
Display Service         privileged Ring 3 service
Aurora Compositor       privileged/session graphics service
Desktop Shell           trusted session client + policy authority
Applications            untrusted clients
Identity System App     trusted pre-session client
```

Early M4 may combine Display Service and compositor implementation in one process for bootstrap, provided the public contracts and ownership boundaries are preserved.

## 4. Capability model

Clients receive explicit capabilities for the graphics objects they may use. A capability identifies an object and permitted rights; possession of a generic IPC endpoint does not imply graphics authority.

Initial logical rights:

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

Ordinary applications must never receive MANAGE_OUTPUTS, MANAGE_WINDOWS or CAPTURE_OUTPUT by default.

## 5. Session isolation

Every normal surface is bound to exactly one session security domain. A surface from one authenticated session must never become visible or readable from another session unless an explicit trusted transition protocol authorizes it.

On logout, the Session Manager revokes the session's graphics authority before the session is considered destroyed.

## 6. Performance direction

The protocol is designed for:

- damage-based recomposition;
- double/triple buffering where useful;
- bounded copies;
- buffer reuse;
- asynchronous frame callbacks;
- frame pacing;
- later zero-copy or GPU-backed buffers;
- later multi-output support.

Correctness must not depend on GPU acceleration.

## 7. Failure behavior

If the compositor or Display Service fails:

- ordinary client presentation stops safely;
- no stale client must acquire new input;
- session graphics capabilities are invalidated/reconstructed on restart;
- the trusted supervisor may restart graphics services with fresh authority;
- boot/recovery framebuffer output remains outside this dependency chain.

## 8. Out of scope for first implementation

- 3D API;
- vendor-specific GPU acceleration;
- HDR;
- variable refresh rate;
- color-management pipeline beyond a stable placeholder contract;
- remote desktop;
- cross-session surface sharing.

These may be added without violating the base ownership model.
