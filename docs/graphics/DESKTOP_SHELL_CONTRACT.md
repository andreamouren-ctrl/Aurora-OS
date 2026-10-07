# Aurora Desktop Shell Contract

Status: **Canonical architecture contract**
Version: **0.1**

## Purpose

The Desktop Shell is the trusted session component that turns compositor primitives into the Aurora desktop experience.

It is intentionally not the compositor itself.

## Responsibilities

The Shell owns policy and UX for:

- desktop/background surfaces;
- panel/dock/launcher;
- task switching;
- window placement and state policy;
- Activity Spaces;
- system overlays;
- notification presentation;
- user customization;
- session lock transition presentation;
- visible permission/privacy indicators when those services exist.

## Privilege

The Shell receives a session-scoped MANAGE_WINDOWS authority and selected privileged surface roles.

It does **not** automatically receive:

- unrestricted filesystem access;
- another user's profile authority;
- credential verification authority;
- screen-capture authority unless explicitly granted;
- direct hardware I/O.

## Activity Space model

An Activity Space is a persistent user work context that can reference:

- participating application/window identities;
- layout/state hints;
- user-selected workspace metadata;
- restoration intents.

Activity persistence must store reconstructible state, not raw live process memory by default.

## Customization

Customization is data-driven and bounded by stable system contracts. Themes/layout choices must not require replacing compositor security rules or forking the window protocol.

## Lock/logout

On lock:

- the Shell yields interactive authority to the trusted lock/pre-session presentation path;
- normal client input is suspended;
- session surfaces may remain allocated but cannot become interactive.

On logout:

- Shell graphics authority is revoked;
- surfaces are destroyed or invalidated;
- session capability teardown occurs before return to pre-session login.

## Restart

A Shell crash should not require kernel failure. The Session Manager may restart it with reconstructed session-scoped authority. The compositor must be able to survive Shell restart without granting policy control to ordinary applications.
