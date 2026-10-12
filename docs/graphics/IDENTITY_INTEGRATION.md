# Aurora Identity Graphics Integration

Status: **Canonical architecture contract**
Version: **0.2**

## First integration slice — trusted presentation ownership

The initial Phase G1 implementation introduces `identity_presentation_decide()` under `kernel/src/ui/identity_presentation.c`:

- **PRE_SESSION:** permits credential input only in allowed Session Manager states and only when no desktop User Session Host is alive.
- **DESKTOP:** permits desktop input only with Session Manager ACTIVE, a live authenticated Ring 3 host and a nonzero session generation.
- **LOCK:** permits credential input only when locked and the old desktop host is no longer alive.
- **QUARANTINE:** an unexpected mixed state allows neither desktop nor credential input.

The normal login UI now avoids framebuffer redraw after the G5 desktop owns scanout. Login/lock transitions stop the old host before displaying trusted prompts. Host policy tests exercise mixed states and a QEMU gate checks the same policy.

### G1 input-epoch hardening (separate acceptance gate)

The trusted login input pump fences the entire routing decision (DESKTOP, LOCK, PRE_SESSION, QUARANTINE and enabled permissions) **and** the Session Manager generation, not merely the coarse domain. On a mismatch it wipes the partially entered local Aurora Key and discards the pending normalized input queue. This blocks stale same-domain events across fresh desktop generations or lock/unlock generations and events crossing READY-to-STARTING permission changes. Host tests cover the pure epoch predicate; boot-validation continues running the routing policy self-test. The input queue has no per-event immutable source-generation tag; therefore this is a bounded queue-epoch defense, not a substitute for future authenticated per-event tagging and the dedicated secure pre-session compositor domain.

**Scope warning:** this is the trusted handoff foundation, not a pre-session compositor-hosted Identity System App yet. The direct framebuffer remains the active login/recovery renderer until a dedicated pre-session client/surface, secure compositor domain and fallback have been tested. Authentication remains solely in Identity Service.

## Goal

Aurora Identity has two presentation paths with different purposes.

### Normal path

A compositor-backed **Aurora Identity System App** is the normal pre-session login and account-management presentation.

### Recovery path

The existing direct-framebuffer login/recovery surface remains independent of the normal compositor stack.

## Boot ordering

Target normal boot sequence:

```text
kernel
 -> protected system/service bootstrap
 -> Aurora Identity Service
 -> Display Service
 -> Aurora Compositor
 -> pre-session host
 -> Aurora Identity System App
 -> authentication
 -> Session Manager
 -> User Session Host
 -> Desktop Shell
```

The exact parallelism may evolve, but authentication authority remains in Identity Service, never in the graphics components.

## Pre-session domain

Before authentication, the compositor runs a restricted pre-session domain.

Only explicitly trusted pre-session clients may create PRE_SESSION surfaces or receive pre-session input.

No authenticated user's profile capability is exposed to the pre-session graphics domain.

## Authentication handoff

Successful authentication produces a Session Manager grant through the Identity architecture. Graphics state alone never proves authentication.

After User Session Host/Desktop Shell readiness:

- pre-session interactive authority is revoked;
- authenticated-session graphics authority becomes active;
- transition may be visually atomic.

## Lock

Lock returns interactive control to a trusted lock presentation without destroying the authenticated session.

Normal application surfaces:

- stop receiving input;
- cannot overlay the lock UI;
- cannot impersonate a trusted unlock surface.

Unlock is authorized by Identity/Session policy, not by compositor state.

## Fallback

If Display Service, compositor, System App, or normal session graphics cannot start, Aurora may use the framebuffer recovery surface.

Fallback must not weaken credential-verification policy. Presentation can degrade; authentication trust requirements cannot.

## Assets

Image assets are presentation resources only. Security state, field contents, focus, prompts and decisions must be rendered dynamically and must not be encoded into static artwork.

Assets must be treated as untrusted file content until decoded by a bounded image loader.

## 2026-10-11 — G2 kernel-owned secure compositor presentation slice

The normal login/lock drawing path can now use an authorized full-screen
`PRE_SESSION` surface in the existing software compositor. The source pixels
are produced by the existing native Identity renderer (canonical PNG artwork,
Aurora Celestia bitmaps, dynamically masked field and status text), staged into
a private bounded framebuffer and copied into a capability-owned graphics
buffer. Scene membership, visibility and hit testing are enforced by the
compositor. The temporary host only owns a READ graphics buffer, a READ/WRITE
graphics surface and DISPLAY CONTROL; no profile/file or authenticated-session
authority is delegated.

The verified V1 display backend is synchronous boot-framebuffer scanout. The
bridge does **not** use the single-buffer policy with asynchronous GPU backends.
On init/present failure the compositor resources are revoked and rendering
returns to the existing independent direct-framebuffer recovery UI without
weakening credential verification. Session activation tears the secure scene
down before the authenticated User Session Host/G5 renderer can publish.

The bootstrap QEMU acceptance requires an actual first `PRE_SESSION` frame
through software composition and display; compile success alone is insufficient.

**Security scope:** this is a *transitional kernel-owned renderer bridge*, not
the final independent Ring 3 Identity System App/secure input service. Aurora
Key handling and credential-field input remain in the trusted kernel controller,
and Identity Service remains the sole authentication authority. The remaining
G2 completion gate is isolated process hosting, secure input ownership and
handoff, lock/unlock and crash/recovery validation, followed by hardware checks.

## 2026-10-12 — Localizzazione coerente del login early stage

Il renderer nativo di Identity usa ora messaggi italiani, in coerenza
con le frasi cosmiche della schermata di boot: prompt Aurora Key,
stati di autenticazione/blocco, invio/ESC, dialogo di creazione.
La chiave `Aurora Key` resta un nome di prodotto, e non viene modificata
la logica di normalizzazione del segreto ne' il protocollo Ring 3.
La localizzazione completa e il cambio lingua con sistema di cataloghi
rimangono obiettivi della successiva Identity System App; il renderer
di recupero supporta intenzionalmente una serie ridotta di caratteri.

## 2026-10-12 — Overlay/cursor suppression on trusted login

While the secure PRE_SESSION compositor scene is active, only the
privileged PRE_SESSION class can contribute output or input hit targets.
Normal windows and even generic SYSTEM_OVERLAY/CURSOR elements are hidden.
This is the first G2 visual non-interference gate; secure Ring 3 hosting,
exclusive physical credential input and lifecycle-negative probes are still
outstanding. The ordinary desktop's overlays and cursors are unchanged.

## 2026-10-12 — G2 isolated Ring 3 host lifecycle (first slice)

An independent `identity-pre-session` Ring 3 binary is now embedded in the
ISO and started after the live Identity/Session Manager bootstrap, before the
first login render. It has its own address space, **one** process-exclusive
READ/WRITE IPC endpoint without TRANSFER, and no access to credentials,
filesystem/profile handles, authenticated sessions, graphics buffers, display
control or raw physical inputs. Kernel launches the process with a bounded
startup ABI (version 1, nonce-like monotonic instance epoch, magic), checks a
Ring 3 bootstrap proof and IPC READY, performs a real IPC health roundtrip,
then can request an acknowledged shutdown and scheduler/process reap.

Boot-validation exercises stop/reap/restart with a fresh epoch, plus a second
health roundtrip. Unexpected process exit is detected without allowing it to
influence authentication. The existing trusted kernel compositor remains
the sole login pixel owner and falls back independently. This **does not yet
transfer presentation or input into Ring 3**: the host currently implements
only isolated process lifecycle and IPC foundation. Next work: constrained
scene/buffer handoff, Ring 3 native layout and fonts, trusted input broker,
lock/unlock crash/epoch recovery, and real-hardware qualification.

## 2026-10-12 — G2 invalid-IPC crash and same-boot reauthentication of host epoch

Boot validation intentionally sends an undefined IPC operation through the
exclusive kernel endpoint after a graceful stop/reap/restart. The Ring 3
runtime must reject it and exit with its fixed protocol-error code; the
kernel verifies the actual exited state, clears the old host lifecycle,
reaps its thread/process/capabilities, and starts a **new process in a strictly
new instance epoch** with a successful health roundtrip. QEMU bootstrap CI
checks the dedicated marker. This test passes no Aurora Key or sensitive
credential buffer to Ring 3, and no profile/file/display authority is
involved. It is not yet a fault-injection test of a real graphical Ring 3
renderer, which still needs a later G2 work package.

## 2026-10-12 — G2 first real Ring 3 pixel-producing PRE_SESSION surface

The isolated `identity-pre-session` Ring 3 process now receives a **small
128x16 visual-only graphics buffer** and its own surface via per-process
non-transferable capabilities. It can map/write only that buffer, attach,
damage and commit its surface through existing graphics syscalls; it does
not receive a display-control capability or direct framebuffer access.
A dedicated IPC draw request is bound to the process instance and must
return an exact request-ID and nonzero commit serial. The kernel
independently verifies the COMMITTED surface snapshot and a known actual
Ring 3-written framebuffer pixel before admitting the surface as a
privileged PRE_SESSION node. Only the compositor's kernel authority may
mark it PRE_SESSION and perform the final scanout.

The small cyan/gold footer accent is positioned outside the password field,
receives no input focus, and contains no personal data. The existing
kernel-owned full-quality login UI remains the sole credential renderer;
loss of the worker or badge only removes a decoration, not login access.
Scene teardown removes the Ring 3 node before revoking buffer/surface
capabilities and releasing graphics objects. The normal compositor
present path emits a serial acceptance marker only after actually
composing the Ring 3-owned pixels into the protected scene. This is an
incremental **real rendering ownership** proof, not yet the full Ring 3
Identity System App; moving the main artwork, Celestia typography, and
trusted interactive field will require separate controlled handoffs.

## 2026-10-12 — Passive Ring 3 layout divider and pointer non-interference

The isolated Ring 3 visual buffer now produces a centered 384x24 non-secret
layout divider at 82% screen height. It contains only cyan/gold geometry,
not a security badge, username, key, input field, text, or trusted-status
claim. The kernel refuses it on displays too small to place it safely;
the full login continues without it.

The compositor now supports an explicit per-node `input_transparent` setting
for privileged PRE_SESSION nodes, with a DISPLAY CONTROL authority check.
Only rendering and occlusion see the node; pointer hit-testing passes
through the decorative Ring 3 layer to the underlying trusted login node.
Kernel verifies this in the live identity scene before admission, and
compositor selftests assert that setting it without display control fails,
generic overlays cannot use this PRE_SESSION policy, and a high-z decorative
PRE_SESSION node never becomes the hit target even while its pixels render.
This closes a mouse-routing interference hazard without moving credentials
or keyboard input to Ring 3. Scene teardown/reaping still removes graphics
nodes before releasing their process-owned capabilities.
