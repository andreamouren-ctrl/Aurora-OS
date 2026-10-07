# Aurora Graphics and Desktop Foundation

Status: **Canonical architecture contract**
Version: **0.1**
Milestone: **M4 — Graphics and Desktop foundation**

This directory defines the contracts that constrain Aurora OS graphics, composition, windowing, input routing, Desktop Shell behavior, and the transition from bootstrap framebuffer UI to the normal compositor-backed environment.

These documents define architecture. They do **not** claim implementation or runtime verification.

## Architectural split

The normal graphics path is:

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

The recovery path remains intentionally independent:

```text
kernel / recovery environment
        |
        v
bootstrap framebuffer renderer
        |
        v
physical output
```

A failure of the normal compositor stack must not remove the ability to present recovery/login diagnostics where a usable boot framebuffer exists.

## Canonical documents

- [ARCHITECTURE.md](ARCHITECTURE.md) — component ownership and trust boundaries.
- [DISPLAY_CONTRACT.md](DISPLAY_CONTRACT.md) — physical-output and scanout contract.
- [SURFACE_BUFFER_PROTOCOL.md](SURFACE_BUFFER_PROTOCOL.md) — client surfaces, buffers, commits and damage.
- [COMPOSITOR_CONTRACT.md](COMPOSITOR_CONTRACT.md) — composition engine responsibilities and guarantees.
- [INPUT_POINTER_CONTRACT.md](INPUT_POINTER_CONTRACT.md) — keyboard/pointer event routing and focus security.
- [WINDOW_MANAGEMENT_CONTRACT.md](WINDOW_MANAGEMENT_CONTRACT.md) — window roles, focus, placement and lifecycle.
- [DESKTOP_SHELL_CONTRACT.md](DESKTOP_SHELL_CONTRACT.md) — Desktop Shell policy and Activity Space integration.
- [IDENTITY_INTEGRATION.md](IDENTITY_INTEGRATION.md) — pre-session Identity graphics and recovery boundary.
- [IMPLEMENTATION_ROADMAP.md](IMPLEMENTATION_ROADMAP.md) — dependency-ordered M4 implementation plan.

## Non-negotiable rules

1. Ordinary applications never map or own the physical display directly.
2. A client owns its content; the compositor owns composition; the Shell owns desktop policy.
3. Window-management policy is not embedded into the low-level Display Service.
4. Surface access is capability-scoped and session-scoped.
5. A surface commit is atomic from the client's point of view.
6. Input is delivered only to authorized targets selected by focus/capture policy.
7. Screen capture is a separate privileged capability, never an implied compositor privilege for ordinary clients.
8. The framebuffer login/recovery renderer remains available independently of the normal desktop path.
9. The first implementation may be CPU-composited, but contracts must not prevent later GPU acceleration.
10. Designed, implemented and runtime-verified states remain distinct.
