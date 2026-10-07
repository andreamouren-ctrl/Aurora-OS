# Aurora Graphics Input and Pointer Contract

Status: **Canonical architecture contract; G4 implementation active**
Version: **0.2**

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

Normalized event families:

- key press/release;
- pointer relative motion;
- pointer absolute motion where supported;
- pointer button press/release;
- scroll axis;
- device add/remove.

The current G4 implementation already feeds normalized keyboard events and an initial PS/2 mouse IRQ12 packet path into this logical layer. USB HID and future input methods must translate into the same event model rather than introducing compositor-visible device-specific formats.

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


## Current G4 implementation status

Implemented foundations:

- normalized keyboard events;
- normalized pointer event representation;
- PS/2 keyboard path;
- initial PS/2 mouse packet decoding on IRQ12;
- preservation/routing rules that prevent auxiliary mouse bytes from being consumed as keyboard data;
- explicit separation between pointer activity and Aurora Identity key-entry handling.

Still pending for G4 acceptance:

- compositor hit testing against committed surface/input regions;
- authoritative pointer focus;
- keyboard focus routing;
- pointer/keyboard capture lifecycle and revocation;
- cross-client leakage negative tests for the completed focus/capture path;
- USB HID transport/device support.
