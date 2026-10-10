# Aurora Identity — Secure PRE_SESSION Surface Visibility

Status: **G2 defense-in-depth surface-class isolation; secure Identity compositor host not yet implemented**  
Date: 2026-10-10

## Trust boundary

The compositor's `AURORA_COMPOSITOR_SURFACE_PRE_SESSION` class is a **privileged surface** and can only be admitted through `software_compositor_add_privileged_surface()` after checking `AURORA_CAP_DISPLAY` with `CONTROL` rights. This class may be visible and hit-testable **only** when `secure_scene_active` is true. An ordinary desktop scene must not expose a PRE_SESSION node even if its trusted owner is late to revoke it.

`software_compositor_set_secure_scene()` requires the same display control authority. The transition invalidates the entire output, so the next full composition clears pre-session pixels before ordinary clients receive scene focus.

The logic in `kernel/src/graphics/software_compositor.c` uses the same `node_allowed_in_scene()` predicate for composition, occlusion and hit-testing. Normal windows are excluded while the secure scene is active; privileged PRE_SESSION content is excluded when it is inactive. SYSTEM_OVERLAY/CURSOR classes preserve their respective existing rules.

## Regression evidence

`software_compositor_selftest()` now checks all three edges:

1. PRE_SESSION node added with privileged authority while secure mode is **inactive**: no hit-test result;
2. after authorized secure-scene activation: PRE_SESSION node is hit-testable and correctly composed, while ordinary desktop pixels are hidden;
3. after authorized deactivation, before node teardown: the PRE_SESSION node becomes non-hit-testable and its prior visible pixel is cleared by full-surface damage; desktop pixels are restored.

The QEMU kernel boot-validation build exercises this compositor self-test via existing graphics gates; do not mark it accepted until the exact-commit QEMU CI passes.

## Remaining Phase G2 work

This security boundary **does not** create a trusted pre-login/lock compositor client, transfer credential input out of the kernel, make an Aurora Identity System App, or certify real-hardware fallback. Those tasks require independent pre-session host capabilities with zero profile delegation, trusted input ownership, session-generation fences, failure/restart tests and framebuffer recovery. Authentication authority remains solely in Identity Service.
