# Aurora Graphics Input and Pointer Contract

Status: **Canonical architecture contract**
Version: **0.1**

## Scope

This contract defines routing from the generic Aurora input layer into graphical sessions. Hardware decoding remains outside the compositor.

## Pipeline

```text
keyboard / mouse / touch hardware
        |
        v
kernel or driver input events
        |
        v
trusted input routing
        |
        +--> Desktop Shell / window policy
        |
        +--> focused authorized client
```

## Device-independent events

The graphical layer consumes normalized events rather than PS/2-specific scan codes.

Initial event families:

- key press/release;
- pointer relative motion;
- pointer absolute motion where supported;
- pointer button press/release;
- scroll axis;
- device add/remove.

USB HID and future input methods translate into the same logical layer.

## Focus

Keyboard focus identifies at most one normal target per seat.

Pointer focus derives from compositor hit testing against the committed input regions and policy constraints.

A client cannot assign itself focus.

## Grabs/capture

Pointer or keyboard capture is scoped, revocable and policy-mediated. A client must not retain capture after:

- its surface is hidden/destroyed;
- session lock;
- logout;
- capability revocation;
- compositor restart.

Global key interception is privileged.

## Secure attention

Aurora reserves a path for security-sensitive input sequences that ordinary applications cannot intercept or synthesize as trusted input.

Exact secure-attention gesture is deferred, but the architecture reserves the boundary now.

## Synthetic input

Synthetic events are explicitly marked and require a privileged automation/accessibility capability. They must never become indistinguishable from hardware-originated secure-attention events.

## Pointer presentation

The compositor may render a cursor surface, but cursor image ownership and pointer-event authority remain separate. A custom cursor cannot expand the application's input region.

## Bounds

Event queues are bounded. Motion coalescing is allowed where it preserves button/key ordering. Key/button state transitions must not be silently reordered.
