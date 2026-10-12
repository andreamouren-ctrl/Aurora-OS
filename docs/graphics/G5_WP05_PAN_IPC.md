# G5 WP-05 — Bounded camera pan IPC v1

**State:** candidate runtime two-window translation/presentation path and QEMU acceptance gate; pending exact-head CI verification.
This is a component of #213, not acceptance of the full WP-05 gate.

`G5_OP_CANVAS_PAN = 0x04000001` uses the existing little-endian
G5 IPC major 1 envelope, an authenticated session generation and
an object generation. The 24-byte payload is:
- bytes 0..3: signed little-endian dx in physical pixels, -32..32;
- bytes 4..7: signed little-endian dy in physical pixels, -32..32;
- bytes 8..15: nonzero expected camera revision (LE uint64);
- bytes 16..23: nonzero expected scene revision (LE uint64).

Both deltas cannot be zero. The opcode demands CONTROL|WRITE in the
receiver's existing capability policy, plus separately checked session,
live surface, scene and camera revisions. Well-formed payloads alone
never grant display or scene authority. No capability handles travel in
the payload; no raw C struct is serialized. All invalid deltas, empty
revisions and missing object generation are rejected *before* a handler.
The candidate runtime handler accepts only the exclusive primary Ring 3
Shell endpoint via the existing G5 receiver authorization, checks
live objects and revisions, stages both window placements, then makes
a single compositor presentation or restores the old policy/geometry.
BOOT_VALIDATION requests two opposite moves from the actual Shell
Ring 3 process, checks distinct independently owned surfaces, new
presentation serials, both policies, rejects stale camera revision,
and verifies unchanged WP-03/WP-04 lifecycle/interaction tests.
The new QEMU workflow marker is mandatory on **both** cold boots.

Production controls are not yet wired to pointer or keyboard navigation.
This is a restricted translation-only dual-window pilot, not pan/zoom
across unbounded Canvas coordinates or release-qualified WP-05.
The next slice must add actual user-triggered navigation, authoritative
persistent world placements, camera state IPC and full scene revisions.

Do not link floating-point host camera references into kernel Ring 0.
