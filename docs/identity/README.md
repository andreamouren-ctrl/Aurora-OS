# Aurora Identity Documentation

Status: **Canonical subsystem specification**
Version: **0.2**

Aurora Identity is the Aurora OS subsystem responsible for local identity, authentication, profile binding, session bootstrap, lock/re-authentication, recovery, and optional trusted authenticators.

The subsystem is intentionally split into three layers:

1. **Aurora Identity System App** — user-facing login, profile, credential, device, recovery, and lock-screen surfaces.
2. **Aurora Identity Service** — isolated privileged user-space service that owns identity records, credential verification, policy, rate limiting, authenticator enrollment, and authenticated-session creation.
3. **Bootstrap / Recovery Login Surface** — minimal framebuffer login path retained outside the normal desktop stack so authentication and recovery remain possible if the compositor or system app cannot start.

The kernel provides mechanisms only: process isolation, capabilities, IPC, protected input/storage access, secure random primitives when available, and session-boundary primitives. Identity policy and credential databases do not belong in the kernel.

## Canonical documents

- [`../AURORA_IDENTITY.md`](../AURORA_IDENTITY.md) — master product specification and current implementation state.
- [`ARCHITECTURE.md`](ARCHITECTURE.md) — component model, trust boundaries, boot/login paths, and process separation.
- [`SECURITY_MODEL.md`](SECURITY_MODEL.md) — threats, security invariants, credential handling, anti-enumeration, throttling, revocation, and audit rules.
- [`AURORA_KEY.md`](AURORA_KEY.md) — Aurora Key format, normalization, generation, storage, change, and verifier rules.
- [`IDENTITY_DRIVE.md`](IDENTITY_DRIVE.md) — removable-drive authenticator model, enrollment, scanning, challenge flow, revocation, and secure-hardware evolution.
- [`IDENTITY_SERVICE.md`](IDENTITY_SERVICE.md) — service responsibilities, IPC contract, state machines, and capabilities.
- [`SYSTEM_APP_UX.md`](SYSTEM_APP_UX.md) — login, creation, lock screen, credential management, device management, and fallback UX.
- [`STORAGE_AND_DATA_MODEL.md`](STORAGE_AND_DATA_MODEL.md) — stable identity records, credential tables, authenticator records, session metadata, migrations, and protected storage requirements.
- [`SESSION_RECOVERY.md`](SESSION_RECOVERY.md) — authenticated sessions, lock/logout, recovery credentials, trusted devices, and emergency recovery behavior.
- [`IMPLEMENTATION_ROADMAP.md`](IMPLEMENTATION_ROADMAP.md) — dependency-ordered implementation plan and acceptance criteria.

## Product invariants

Aurora Identity must always preserve these rules:

- local login works without cloud connectivity;
- the default login screen does not enumerate accounts;
- the stable `user_id` is independent from every credential;
- raw Aurora Keys are never persisted;
- applications never receive login credentials;
- authentication policy remains outside Ring 0;
- removable authenticators are individually revocable;
- losing one authenticator must not destroy the profile;
- recovery is a distinct credential path, not reversible Aurora Key storage;
- the bootstrap/recovery path must remain usable even when normal desktop services fail.

## Authentication methods

Aurora Identity is designed to support multiple methods bound to one stable identity:

```text
Aurora Identity
└── user_id
    ├── Aurora Key
    ├── Aurora Identity Drive #1
    ├── Aurora Identity Drive #2
    ├── future secure hardware key
    ├── recovery credential
    └── optional trusted-device credential
```

The Aurora Key remains the baseline local credential. Other authenticators are alternatives or additional factors; they do not replace the stable identity record.

## Current repository state

Already implemented:

- boot-to-login framebuffer handoff;
- native Aurora Identity framebuffer login prototype;
- generic kernel input-event queue;
- IRQ-driven PS/2 keyboard prototype;
- alphanumeric Aurora Key entry, masking, Backspace, Enter and Esc handling.

Not yet implemented:

- persistent Aurora Identity Service;
- secure random service suitable for credential generation;
- audited Argon2id verifier path;
- persistent identity database;
- removable-storage stack required for Aurora Identity Drive;
- session manager and authenticated profile bootstrap;
- compositor-backed Aurora Identity System App;
- full recovery environment.
