#ifndef AURORA_G5_CANVAS_PROJECTION_H
#define AURORA_G5_CANVAS_PROJECTION_H

/* WP-05 pure user-space projection oracle, NOT a compositor IPC API.
 * Projection is non-authoritative: capability, session and scene ownership
 * must be checked by the trusted coordinator before committing a frame.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <aurora/g5_canvas_camera.h>

/* Current G3 software compositor is bounded to 64 visible nodes. */
#define G5_CANVAS_PROJECTION_MAX 64u

struct g5_canvas_panel {
    uint64_t object_id;
    uint64_t object_generation;
    struct g5_world_aabb bounds;
};

/* Pixel rectangle fully inside the viewport, with conservative
 * 1-pixel inflation around the projected world-space object bounds.
 * The compositor must still validate live surface caps and clip content.
 */
struct g5_canvas_projected_panel {
    uint64_t object_id;
    uint64_t object_generation;
    int32_t x_px, y_px;
    uint32_t width_px, height_px;
};

/* A stale session/scene/camera revision fails closed. No output is written
 * on invalid arguments or invisible objects. out_visible is written only
 * after the call validates and computes the result successfully. */
bool g5_canvas_project_panel(
    const struct g5_camera *camera,
    uint64_t expected_camera_revision,
    uint64_t current_scene_revision,
    uint64_t expected_scene_revision,
    uint64_t session_generation,
    const struct g5_canvas_panel *panel,
    struct g5_canvas_projected_panel *out,
    bool *out_visible
);

/* In-bounds batch; unique object IDs and nonzero generations required.
 * Stage and validate every output before publication; any failure leaves
 * out_count and output storage unchanged. Never permits >64 visible nodes.
 * Revisions/authority must be pinned by an external trusted caller while
 * the two reference passes execute.
 */
bool g5_canvas_project_batch(
    const struct g5_camera *camera,
    uint64_t expected_camera_revision,
    uint64_t current_scene_revision,
    uint64_t expected_scene_revision,
    uint64_t session_generation,
    const struct g5_canvas_panel *panels,
    size_t panel_count,
    struct g5_canvas_projected_panel *out,
    size_t out_capacity,
    size_t *out_count
);
#endif
