# G5 WP-05 — Bounded camera pan IPC v1

**State:** protocol source implemented; no Ring 3 runtime camera dispatch yet.
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
The next runtime slice must implement a trusted handler which accepts
only the exclusive primary Ring 3 Shell endpoint, stages both window
placements, publishes a single compositor frame and rolls back on
failure. The 32px bound is an initial QEMU viewport pilot, **not**
the final pan/zoom range or spatial camera API.

Do not link floating-point host camera references into kernel Ring 0.
