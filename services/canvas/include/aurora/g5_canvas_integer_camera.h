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
