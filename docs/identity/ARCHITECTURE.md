# Aurora Identity Architecture

Status: **Canonical design**
Version: **0.2**

## 1. Purpose

Aurora Identity is not a single executable. It is a coordinated security subsystem composed of a privileged identity service, a user-facing system app, session/bootstrap integration, and a minimal recovery-capable fallback surface.

The design follows Aurora OS's modular hybrid capability-kernel model: Ring 0 provides mechanisms, while identity policy remains in isolated user space.

## 2. Component model

```text
+-----------------------------------------------------------+
| Aurora Identity System App                               |
| Login / Create / Lock / Credentials / Devices / Recovery |
+------------------------------+----------------------------+
                               |
                               | narrow authenticated IPC
                               v
+-----------------------------------------------------------+
| Aurora Identity Service                                  |
| identity DB | verifiers | policy | authenticators        |
| throttling  | recovery  | session authorization          |
+-------------------+----------------------+----------------+
                    |                      |
                    | capabilities         | session result
                    v                      v
+--------------------------+     +---------------------------+
| Storage / Secret / RNG   |     | Aurora Session Manager    |
| services                 |     | profile/session bootstrap |
+--------------------------+     +---------------------------+
                    ^
                    |
+-----------------------------------------------------------+
| Kernel mechanisms                                          |
| IPC | capabilities | process isolation | input | devices   |
+-----------------------------------------------------------+
```

A separate framebuffer bootstrap surface is retained for early boot and recovery:

```text
Boot UI -> Bootstrap Login Surface -> Identity Service -> Session
                       |
                       +-> Recovery mode if desktop stack fails
```

When the normal graphics/session stack exists, the **Aurora Identity System App** becomes the primary login UI. The framebuffer surface becomes a fallback, not the normal long-term presentation layer.

## 3. Process boundaries

### 3.1 Aurora Identity Service

Runs in its own isolated user-space process with narrowly scoped capabilities. It is the only ordinary component allowed to read/write the protected identity database and invoke credential-verification primitives.

It must not expose raw verifier material to the UI.

### 3.2 Aurora Identity System App

Runs as a trusted system application but remains separated from the credential database. It receives only the minimum IPC rights needed to:

- submit credential material;
- initiate user creation when policy allows;
- enroll/revoke authenticators;
- query non-secret account-management metadata for the currently authenticated identity;
- request lock/logout/re-authentication;
- begin recovery workflows.

The app must not gain unrestricted filesystem access merely because it is system software.

### 3.3 Session Manager

Consumes an authenticated session grant, not the original Aurora Key or authenticator secret. It binds the stable `user_id` to profile, settings, desktop services, and user capabilities.

### 3.4 Input and device services

Keyboard, USB mass storage, future USB HID/security-key devices, and removable-media discovery remain device/input responsibilities. Aurora Identity consumes normalized events and device capabilities instead of containing hardware-specific code.

## 4. Stable identity versus credentials

The stable identity is represented by a system-generated opaque `user_id`.

Credentials are replaceable authentication methods bound to that identity.

```text
user_id: 7f...opaque...
  credential A: Aurora Key verifier
  credential B: Identity Drive credential
  credential C: recovery credential
```

Changing or revoking a credential must not change the profile path or `user_id`.

## 5. Boot and login paths

### 5.1 Normal future path

1. Kernel completes boot mechanisms.
2. Core services start.
3. Aurora Identity Service starts and opens protected identity storage.
4. Display/compositor/session host starts the Aurora Identity System App in pre-session mode.
5. User authenticates using Aurora Key or an approved authenticator.
6. Identity Service returns an opaque authenticated-session grant.
7. Session Manager validates the grant and starts the user's session.
8. Pre-session Identity App transitions out or changes to account-management mode.

### 5.2 Current bootstrap path

Today the repository uses a direct framebuffer login surface because the compositor and final service environment do not yet exist. This path is intentionally temporary for normal operation but permanent as a recovery capability.

### 5.3 Recovery path

If the normal compositor, system app, or desktop session fails to initialize, Aurora may enter a minimal recovery login surface with only:

- Aurora Key entry;
- supported recovery credentials;
- diagnostic-safe status messages;
- repair/recovery actions explicitly authorized by the authenticated identity.

## 6. Identity Service startup dependency

The service requires, at minimum:

- process isolation;
- IPC and capability transfer;
- persistent storage/VFS;
- protected system data area;
- secure random source;
- monotonic time source for throttling/session expiry;
- audited credential derivation/verification implementation.

Aurora Identity Drive additionally requires:

- removable-storage discovery;
- filesystem support for its credential container;
- device insertion/removal events;
- protected read path exposed to Identity Service or a brokered removable-media service.

## 7. Capability model

Expected conceptual capabilities include:

- `identity.db.readwrite` — Identity Service only;
- `identity.authenticate` — pre-session Identity App and fallback login surface;
- `identity.manage.self` — authenticated Identity App for current user;
- `identity.manage.machine` — restricted administrative/managed-system policy path;
- `identity.session.issue` — Identity Service only;
- `session.accept_identity` — Session Manager;
- `removable.enumerate.identity` — brokered access to eligible removable authenticators;
- `rng.secure` — credential generation service;
- `audit.identity.write` — structured, non-secret security events.

Exact capability identifiers remain subject to the final system-service API conventions.

## 8. IPC philosophy

Identity APIs are asynchronous from the UI perspective. A request receives a request/session identifier and completes through a response/event path.

The contract must support cancellation when the user leaves the login screen or removes a device.

Credential-bearing IPC buffers should be:

- bounded;
- short-lived;
- non-loggable;
- cleared after consumption when practical;
- unavailable to unrelated processes.

## 9. Failure isolation

A crash of the Aurora Identity System App must not corrupt the identity database.

A crash of the Identity Service must fail closed: no session is started without a valid authenticated grant.

A malformed removable drive must be treated as untrusted input and must not crash the service or kernel.

Database migrations must be transactional and recoverable.

## 10. Non-goals for Ring 0

The kernel must not:

- decide whether an unknown Aurora Key may create a user;
- store password verifiers;
- parse identity database policy;
- own recovery policy;
- scan removable filesystems for identity credentials directly when a user-space storage service can do so;
- display normal long-term account-management UI.

## 11. Long-term evolution

The architecture permits later addition of:

- secure hardware/FIDO-style authenticators;
- biometric brokers where supported;
- enterprise/managed identity policy;
- encrypted multi-device synchronization;
- trusted-device approval;
- remote recovery assistance.

None of these may make ordinary local authentication depend on the network.
