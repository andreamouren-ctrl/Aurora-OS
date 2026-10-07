# Aurora Surface and Buffer Protocol

Status: **Canonical architecture contract**
Version: **0.1**

## 1. Principle

Applications render content into buffers they own or are explicitly granted. They submit buffers to compositor-owned surfaces. A surface is not a physical window and does not imply placement authority.

## 2. Objects

### Buffer

A bounded memory object containing pixels or another explicitly negotiated representation.

Required metadata:

- width;
- height;
- stride;
- format;
- byte length;
- storage type;
- release state.

Initial required format: premultiplied 32-bit RGBA/BGRA-class format selected by implementation and frozen before ABI stabilization.

### Surface

A compositor object holding pending and committed visual state.

Surface state includes:

- attached buffer;
- damage region;
- logical size;
- buffer scale;
- transform;
- opaque region;
- input region;
- role;
- optional parent relationship.

## 3. Atomic commit model

Client changes accumulate in **pending state**. COMMIT atomically promotes a complete validated pending state to **committed state**.

Invalid state rejects the commit without partially changing the visible surface.

## 4. Buffer lifecycle

```text
FREE
 -> client rendering
 -> ATTACHED/PENDING
 -> COMMITTED
 -> IN_USE
 -> RELEASED
 -> reusable by client
```

A client must not overwrite a buffer while the compositor may still read it.

## 5. Damage

Clients report regions changed since the prior committed content. The compositor may ignore damage and repaint more, but it must never assume pixels outside a declared valid buffer exist.

The protocol bounds region count and coordinates to prevent unbounded CPU or memory use.

## 6. Roles

A surface has at most one semantic role at a time.

Initial roles:

- TOPLEVEL
- POPUP
- SYSTEM_OVERLAY
- DESKTOP_BACKGROUND
- CURSOR
- PRE_SESSION

Privileged roles require privileged capabilities. An ordinary client cannot self-assign SYSTEM_OVERLAY, CURSOR or PRE_SESSION.

## 7. Parent/child surfaces

Child surfaces may be used for menus, popups or subsurfaces. Parent destruction invalidates or detaches descendants according to role rules; orphaned privileged surfaces are forbidden.

## 8. Backpressure

The compositor may throttle clients that submit faster than presentation. Clients request frame callbacks and should render the next frame only when useful.

Queues are bounded. Resource exhaustion returns an explicit error rather than allocating without limit.

## 9. Security

- Buffer size and metadata are validated before mapping.
- Integer multiplication/addition used for pixel addressing is checked.
- Client-provided coordinates never become trusted pointers.
- Buffer mapping does not grant access to another client's memory.
- Surface handles are unforgeable capability-backed objects.
- Destroy/revoke is idempotent from the caller's recovery perspective.
