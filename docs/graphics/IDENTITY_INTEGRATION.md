# Aurora Identity Graphics Integration

Status: **Canonical architecture contract**
Version: **0.1**

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
