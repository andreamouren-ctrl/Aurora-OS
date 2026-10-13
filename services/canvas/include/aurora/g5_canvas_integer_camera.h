#ifndef AURORA_G5_CANVAS_INTEGER_CAMERA_H
#define AURORA_G5_CANVAS_INTEGER_CAMERA_H
#include <stdbool.h>
#include <stdint.h>
/* Freestanding integer-only WP-05.A arithmetic candidate.
 * Persistent world positions use the canonical 1024 ticks/unit.
 * zoom_q16 is pixels per world unit at device scale 1, Q16.16.
 * No floating point, libc math, allocation or kernel dependency. */
#define G5_ICAM_ONE UINT32_C(65536)
#define G5_ICAM_MIN_ZOOM UINT32_C(1024)
#define G5_ICAM_MAX_ZOOM UINT32_C(4194304)
struct g5_integer_camera {
    int64_t center_x_ticks, center_y_ticks;
    uint32_t zoom_q16, width_px, height_px;
    uint64_t revision;
};
bool g5_icam_init(struct g5_integer_camera *camera, int64_t x, int64_t y,
                   uint32_t zoom_q16, uint32_t width, uint32_t height);
bool g5_icam_set_viewport(struct g5_integer_camera *camera,
                            uint32_t width, uint32_t height);
bool g5_icam_pan(struct g5_integer_camera *camera, int32_t dx_px, int32_t dy_px);
/* Factor in Q16.16; zoom anchored at an integer screen pixel. */
bool g5_icam_zoom_at(struct g5_integer_camera *camera, uint32_t factor_q16,
                      int32_t pointer_x_px, int32_t pointer_y_px);
bool g5_icam_project(const struct g5_integer_camera *camera,
                      int64_t x_ticks, int64_t y_ticks,
                      int32_t *x_px, int32_t *y_px);
bool g5_icam_unproject(const struct g5_integer_camera *camera,
                        uint64_t expected_revision, int32_t x_px, int32_t y_px,
                        int64_t *x_ticks, int64_t *y_ticks);
/* World-space rectangles are immutable scene geometry. Their projected
 * bounds are derived from the committed camera, never stored back into
 * the scene's world coordinates. Screen bounds may be off-viewport. */
struct g5_icam_screen_rect {
    int32_t x_px,y_px,width_px,height_px;
};
bool g5_icam_project_rect(const struct g5_integer_camera *camera,
                           int64_t x_ticks,int64_t y_ticks,
                           uint32_t width_ticks,uint32_t height_ticks,
                           struct g5_icam_screen_rect *out);
/* Clip a projected world rectangle to the camera viewport. The
 * returned source offsets allow the renderer to crop instead of moving
 * or mutating the original world-space window. Fully offscreen objects
 * return false without touching the output. */
struct g5_icam_clip_rect {
    int32_t x_px,y_px,width_px,height_px;
    int32_t source_x_px,source_y_px;
};
bool g5_icam_clip_to_viewport(
    const struct g5_integer_camera *camera,
    const struct g5_icam_screen_rect *projected,
    struct g5_icam_clip_rect *out);
/* Resolve pointer focus against immutable world-space rectangles.
 * The caller must provide rectangles in front-to-back stacking order;
 * hidden/offscreen objects can be skipped before calling. The pointer
 * coordinate is viewport-local and never alters the camera. */
bool g5_icam_hit_world_rect(const struct g5_integer_camera *camera,
                             uint64_t camera_revision,
                             int32_t pointer_x_px,int32_t pointer_y_px,
                             int64_t world_x_ticks,int64_t world_y_ticks,
                             uint32_t width_ticks,uint32_t height_ticks);
/* Atomic two-window camera layout. World rectangles are never modified.
 * If either window cannot be projected, neither output is published.
 * The renderer can consume both results in one compositor transaction. */
struct g5_icam_world_rect {
    int64_t x_ticks,y_ticks;
    uint32_t width_ticks,height_ticks;
};
bool g5_icam_project_pair(
    const struct g5_integer_camera *camera,uint64_t expected_revision,
    const struct g5_icam_world_rect *first,
    const struct g5_icam_world_rect *second,
    struct g5_icam_screen_rect *first_out,
    struct g5_icam_screen_rect *second_out);
/* Ring 3 compositor submission: bounded, immutable world geometry.
 * An offscreen object is represented by visible=false; an arithmetic
 * failure rejects the entire batch without changing any output. The
 * compositor must authenticate the surfaces before presenting it. */
#define G5_ICAM_SCENE_MAX_RECTS UINT32_C(16)
struct g5_icam_scene_item {
    struct g5_icam_world_rect world;
    uint64_t surface_id;
};
struct g5_icam_projected_item {
    uint64_t surface_id;
    struct g5_icam_screen_rect projected;
    struct g5_icam_clip_rect clip;
    bool visible;
};
bool g5_icam_project_scene(
    const struct g5_integer_camera *camera,uint64_t expected_revision,
    const struct g5_icam_scene_item *items,uint32_t count,
    struct g5_icam_projected_item *out);

/* Hit-test the same projected scene snapshot used for presentation.
 * Items are ordered back-to-front (last is topmost). Returns a surface
 * identifier, or zero for background. The revision fence prevents using
 * a stale rendered scene after the camera has moved. */
bool g5_icam_hit_projected_scene(
    const struct g5_integer_camera *camera,uint64_t expected_revision,
    const struct g5_icam_projected_item *items,uint32_t count,
    int32_t pointer_x_px,int32_t pointer_y_px,
    uint64_t *out_surface_id);

/* Compositor handoff: a Ring 3-owned immutable frame transaction.
 * The compositor must verify surface capabilities and session generation
 * before accepting it; this structure alone grants no display rights.
 * A presentation ACK is matched to both camera and scene revisions. */
struct g5_icam_scene_submission {
    uint64_t session_generation;
    uint64_t scene_revision;
    uint64_t request_id;
    struct g5_integer_camera camera;
    uint32_t count;
    struct g5_icam_projected_item items[G5_ICAM_SCENE_MAX_RECTS];
};
bool g5_icam_prepare_submission(
    const struct g5_integer_camera *camera,uint64_t expected_camera_revision,
    uint64_t session_generation,uint64_t scene_revision,uint64_t request_id,
    const struct g5_icam_scene_item *items,uint32_t count,
    struct g5_icam_scene_submission *out);
bool g5_icam_match_presented(
    const struct g5_icam_scene_submission *submitted,
    uint64_t session_generation,uint64_t scene_revision,
    uint64_t request_id,uint64_t camera_revision,uint64_t display_serial,
    uint64_t previous_display_serial);

/* Only a frame acknowledged as presented may be used to route focus.
 * Compare against the live Shell camera and scene revisions so an old
 * rendered frame cannot receive input after navigation or scene changes. */
bool g5_icam_hit_presented_submission(
    const struct g5_icam_scene_submission *submitted,
    const struct g5_integer_camera *live_camera,
    uint64_t live_scene_revision,uint64_t presented_display_serial,
    int32_t pointer_x_px,int32_t pointer_y_px,uint64_t *out_surface_id);

/* Commit a staged camera only after the compositor presents the exact
 * submitted frame. The live camera and serial remain unchanged on any
 * failed acknowledgement. This is a pure Ring 3 state transition, not
 * an IPC authentication substitute. */
bool g5_icam_commit_presented_camera(
    struct g5_integer_camera *live_camera,
    uint64_t *last_display_serial,
    const struct g5_icam_scene_submission *submitted,
    uint64_t session_generation,uint64_t scene_revision,
    uint64_t request_id,uint64_t camera_revision,
    uint64_t display_serial);

/* Compact, explicit LE scene-batch payload for the existing G5 envelope.
 * Max 3 objects per packet; the caller must reject larger frames or use
 * a future authenticated shared-buffer protocol, never truncate.
 * This is serialization only: raw surface IDs are not capabilities. */
#define G5_ICAM_WIRE_BATCH_MAX 3u
#define G5_ICAM_WIRE_BATCH_HEADER 56u
#define G5_ICAM_WIRE_BATCH_ITEM 48u
#define G5_ICAM_WIRE_BATCH_CAPACITY (G5_ICAM_WIRE_BATCH_HEADER + \
                                    G5_ICAM_WIRE_BATCH_MAX * G5_ICAM_WIRE_BATCH_ITEM)
bool g5_icam_encode_batch(
    const struct g5_icam_scene_submission *submitted,
    uint8_t *destination,uint32_t capacity,uint32_t *written);
bool g5_icam_decode_batch(
    const uint8_t *source,uint32_t length,
    struct g5_icam_scene_submission *out);

/* Shell-owned camera state must be synchronized as a complete snapshot.
 * A revision alone does not prove center, zoom or viewport equivalence.
 * expected_revision fences out-of-order acknowledgements. The caller
 * validates authenticated session/object generations at the IPC boundary.
 * This pure Ring 3 helper neither grants authority nor calls the kernel. */
bool g5_icam_accept_snapshot(struct g5_integer_camera *committed,
                              uint64_t expected_revision,
                              const struct g5_integer_camera *snapshot);
/* Legacy bounded-pan pilot compatibility only. Do not use revision-only
 * reconciliation for the full spatial camera protocol. */
/* A rejected transaction never publishes speculative position or zoom.
 * The kernel's authoritative revision may be newer than the request's.
 * A backwards revision or overflow sentinel is not a valid resync. */
static inline bool g5_icam_reconcile_rejected(
    struct g5_integer_camera *committed,uint64_t kernel_revision) {
    if (!committed || kernel_revision<committed->revision ||
        kernel_revision==UINT64_MAX)
        return false;
    committed->revision=kernel_revision;
    return true;
}
#endif
