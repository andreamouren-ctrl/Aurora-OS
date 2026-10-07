# Aurora Identity Test and Verification Plan

Status: **Canonical verification plan**
Version: **0.2**

## 1. Purpose

Aurora Identity is security-critical. A feature is not considered complete solely because the UI works. Each implementation phase must include correctness, failure, persistence, security, and performance verification.

## 2. Test levels

Aurora Identity testing is divided into:

- unit tests for normalization, parsers, state machines, and bounded data structures;
- service integration tests for IPC, storage, verifier, rate limiting, and session grants;
- boot/runtime tests in QEMU/VM environments;
- hardware tests for USB/removable authenticators;
- fuzzing for untrusted removable-media/container parsing;
- fault-injection tests for interrupted writes and service crashes;
- security review tests for replay, enumeration, throttling, and secret leakage;
- performance benchmarks for authentication/session latency and memory cost.

## 3. Aurora Key tests

Required cases:

- valid 12-character key;
- valid 32-character key;
- lowercase normalization;
- spaces/hyphens ignored where accepted;
- invalid characters rejected;
- below-minimum rejected;
- above-maximum bounded/rejected;
- Backspace behavior;
- Esc clears credential;
- Enter submission;
- raw key absent from logs;
- verifier survives reboot;
- wrong key fails;
- key rotation invalidates old credential and preserves profile;
- parameter migration after successful authentication.

## 4. Identity creation tests

- unknown valid key + creation enabled -> creation offer;
- creation disabled -> no account creation path;
- cancel clears secret state;
- interrupted creation leaves no partially login-capable user;
- duplicate/normalized credential conflict handled atomically;
- profile binding uses stable `user_id` rather than display name;
- reboot after creation preserves identity.

## 5. Rate-limit tests

- repeated failures trigger progressive delay;
- throttle state survives reboot;
- successful authentication resets/decays state according to policy;
- unknown-key attack cannot cause unbounded database growth;
- machine-wide pressure limit works;
- UI does not reveal unsafe internal throttle/account state;
- clock anomalies do not produce permanent lockout or bypass.

## 6. Session grant tests

- valid grant starts correct `user_id` session;
- grant is single-use;
- replay fails;
- expired grant fails;
- wrong Session Manager/caller cannot consume grant;
- logout invalidates session-scoped access;
- lock revokes interactive delegated profile capabilities while preserving the underlying session binding;
- lock requires fresh authentication;
- unlock with a different valid user_id is rejected and leaves the original session locked;
- same-user fresh authentication restores the session profile lease and user host;
- purpose-bound re-auth proof cannot authorize another operation.

## 7. Identity Drive tests

### Enrollment

- enroll eligible USB drive;
- no Aurora Key is written to drive;
- local authenticator record created transactionally;
- enrollment verification must pass before success is reported;
- multiple drives may be enrolled;
- friendly labels do not affect authentication.

### Login

- enrolled drive authenticates offline;
- revoked drive fails;
- unknown drive fails safely;
- drive removal mid-authentication cancels safely;
- multiple connected drives handled deterministically;
- optional PIN mode works;
- automatic-authentication policy obeyed.

### Malformed media

- missing container;
- oversized container;
- truncated container;
- invalid magic/version;
- corrupted integrity field;
- duplicate fields;
- malformed lengths/counts;
- unexpected filesystem errors;
- rapid insertion/removal;
- parser fuzz corpus.

None may crash the kernel or privileged service.

## 8. Standard-drive cloning tests

Because ordinary USB mass storage can be clonable, tests must confirm product behavior is consistent with the documented security tier:

- copied container does not gain access after authenticator revocation;
- duplicate simultaneous use is handled safely;
- device serial/VID/PID/label is never the sole proof;
- optional machine binding is enforced cryptographically by local enrollment data;
- UI does not falsely classify standard drives as non-exportable hardware keys.

## 9. Secure hardware authenticator tests

When implemented:

- fresh challenge for every authentication;
- replayed signature/proof rejected;
- public-key enrollment bound to correct identity;
- private key cannot be exported through Aurora APIs;
- device removal cancels request;
- revoked hardware key fails;
- malformed protocol messages fail closed.

## 10. Recovery tests

- valid recovery credential succeeds;
- invalid recovery credential throttled;
- recovery never reveals old Aurora Key;
- successful recovery requires/causes Aurora Key rotation according to policy;
- old Aurora Key fails afterward;
- recovery audit event written without secrets;
- lost-drive revocation works after recovery;
- no recovery method -> authentication cannot be bypassed.

## 11. Storage/database fault tests

Inject failure during:

- identity creation;
- key rotation;
- authenticator enrollment;
- authenticator revocation;
- schema migration;
- rate-limit update;
- last-login metadata update.

After reboot/restart, the database must resolve to a valid pre- or post-transaction state, never a half-authenticated state.

## 12. Service isolation tests

- ordinary app cannot open identity database;
- ordinary app cannot call privileged management APIs;
- caller without authentication capability cannot submit login requests where not allowed;
- Identity System App cannot obtain raw verifier records;
- Session Manager cannot request stored credentials;
- compromised UI process cannot mint session grants.

## 13. Privacy and secret-leak tests

Search runtime artifacts for credential leakage:

- serial log;
- kernel log;
- crash/panic output;
- service logs;
- audit log;
- shell/command history;
- clipboard history;
- temporary files;
- swap/page files once implemented;
- screenshots/debug capture where protected-input policy applies.

Tests should use known synthetic secrets so automated scans can detect accidental copies.

## 14. Account-enumeration tests

Compare visible/timing behavior for:

- existing identity + wrong secret;
- non-existing key;
- disabled identity;
- throttled identity.

Perfect timing equality may not always be practical, but the UI/API must avoid unnecessary explicit disclosure and obvious fast-path leaks.

## 15. Performance benchmarks

Measure at minimum:

- Aurora Key verification latency;
- Argon2id memory usage;
- Identity Service idle CPU wakeups;
- login input-to-render latency;
- identity database open/migration latency;
- session grant issue/consume latency;
- profile startup latency;
- Identity Drive detection-to-authentication latency.

Argon2id parameters must be chosen from benchmark data rather than arbitrary constants.

## 16. CI gates

As infrastructure becomes available, CI should include:

- freestanding/kernel build remains clean;
- boot smoke test reaches login/session milestone;
- unit/integration identity tests;
- database migration tests;
- secret-log scanning using synthetic credentials;
- session replay tests;
- malformed container corpus tests.

Hardware-specific USB tests may initially run outside hosted CI but should have reproducible scripts/test cases.

## 17. Release gate

Aurora Identity cannot be marked production-ready until all relevant phase acceptance tests pass, known security limitations are documented, and no open critical issue allows authentication bypass, credential disclosure, session replay, or silent revocation failure.
