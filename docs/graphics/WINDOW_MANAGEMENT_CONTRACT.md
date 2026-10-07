# Aurora Window Management Contract

Status: **Canonical architecture contract**
Version: **0.1**

## Principle

Window management is policy layered over compositor surfaces. Applications request state; trusted window policy decides placement and activation.

## Toplevel model

A TOPLEVEL surface may request:

- title/metadata;
- minimum/maximum logical size;
- resizable state;
- fullscreen;
- maximized;
- minimized;
- attention;
- activation.

Requests are not unilateral authority.

## Configure/ack protocol

For policy-controlled size or state changes:

1. policy sends CONFIGURE with a serial and target state;
2. client renders an appropriate buffer;
3. client ACKs the serial;
4. client commits;
5. compositor/policy makes the new state visible.

This prevents the desktop from assuming a client has already rendered a requested size.

## Activation

Activation/focus requests require a recent valid user-interaction token or trusted Shell authority. Background applications cannot steal focus by repeatedly requesting activation.

## Placement

Initial policy may use simple centered/cascaded placement. The protocol must permit later tiled, floating, snapped and Activity Space-specific placement without changing client rendering semantics.

## Decorations

Aurora reserves both:

- server/Shell-managed decorations;
- client-side decorations for approved use cases.

Security-significant surfaces may require trusted decorations/indicators.

## Popups

Popups are transient children constrained to an authorized parent and output work area. They cannot create an invisible input-capture region outside policy bounds.

## Fullscreen

Fullscreen is policy-granted and output-scoped. System security overlays remain able to appear above fullscreen applications.

## Lifecycle

A window can be created before first mapping. Unmapped surfaces receive no ordinary pointer/keyboard input.

Closing a window is a request to the client first where possible; policy may force termination through process lifecycle controls when necessary.

## Activity Spaces

A toplevel belongs to an Activity Space or a Shell-defined global/system scope. Moving between spaces is Shell policy and does not transfer client memory ownership.
