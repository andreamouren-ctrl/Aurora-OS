# ADR-0003 — Aurora Identity Service / System App Split

Status: Accepted
Date: 2026-10-01

## Decision

Aurora Identity will be implemented as a security subsystem composed of:

1. an isolated **Aurora Identity Service** in user space that owns identity records, credential verification, authenticator policy, throttling, recovery policy, and authenticated-session grant issuance;
2. an **Aurora Identity System App** that owns login, profile setup, account management, access-device management, lock/re-authentication, and recovery presentation;
3. a minimal **bootstrap/recovery login surface** retained independently from the normal compositor-backed application path.

Identity policy, credential verifiers, and the persistent user database will not be placed in the kernel.

## Reasons

- Aurora OS already defines a modular hybrid capability-kernel architecture;
- credential/database policy does not require Ring 0 privileges;
- isolating the Identity Service reduces the blast radius of UI and parser failures;
- the System App can evolve visually without changing the authentication trust boundary;
- the fallback surface preserves login/recovery when the normal graphics stack fails;
- capabilities and IPC provide a narrow contract between UI, identity authority, storage, and Session Manager;
- the design supports future Aurora Identity Drive and secure-hardware authenticators without coupling USB parsing to the kernel login UI.

## Consequences

- the current framebuffer login remains a prototype and future recovery fallback rather than the final normal login application;
- persistent identity work depends on storage/VFS, secure RNG, and an isolated user-space service lifecycle;
- the Session Manager consumes opaque authenticated grants rather than credentials;
- the Aurora Identity System App must not read the protected identity database directly;
- removable authenticators are brokered through device/storage services and verified by the Identity Service;
- ordinary local authentication remains available offline.
