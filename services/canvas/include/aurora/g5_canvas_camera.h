#ifndef AURORA_G5_CANVAS_CAMERA_H
#define AURORA_G5_CANVAS_CAMERA_H

/* G5 WP-05 reference camera. User-space only: never link into Ring 0.
 * Storage/wire formats must serialize each field explicitly, not this struct.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define G5_CANVAS_TICKS_PER_UNIT INT64_C(1024)
#define G5_CAMERA_MIN_ZOOM (1.0 / 64.0)
#define G5_CAMERA_MAX_ZOOM (64.0)
#define G5_CAMERA_MIN_DEVICE_SCALE (0.25)
#define G5_CAMERA_MAX_DEVICE_SCALE (4.0)
#define G5_CAMERA_MAX_VIEWPORT 16384u

typedef int64_t g5_world_coord_t;
struct g5_world_point { g5_world_coord_t x, y; };
struct g5_world_aabb {
    struct g5_world_point min, max;
};

/* World anchor is exact; sub-tick camera offset is transient.
 * Camera revision changes on every successful viewport transform mutation.
 * Scene revision is a separate, independent WP-06 concern.
 */
struct g5_camera {
    struct g5_world_point anchor;
    double offset_x_units, offset_y_units;
    double zoom, device_scale;
    uint32_t width_px, height_px;
    uint64_t revision;
};

bool g5_world_add_checked(int64_t a, int64_t b, int64_t *out);
bool g5_world_translate_checked(struct g5_world_point p,
                                int64_t dx, int64_t dy,
                                struct g5_world_point *out);
bool g5_world_aabb_valid(const struct g5_world_aabb *bounds);

bool g5_camera_init(struct g5_camera *camera,
                    struct g5_world_point anchor,
                    uint32_t width_px, uint32_t height_px,
                    double zoom, double device_scale);
bool g5_camera_valid(const struct g5_camera *camera);
bool g5_camera_set_viewport(struct g5_camera *camera,
                            uint32_t width_px, uint32_t height_px);
bool g5_camera_pan(struct g5_camera *camera, double dx_px, double dy_px);
bool g5_camera_zoom_at(struct g5_camera *camera, double factor,
                        double pointer_x_px, double pointer_y_px);
/* Returns false for invalid camera or offscreen/out-of-range point.
 * Rounds compositor pixel positions to nearest, halves away from zero. */
bool g5_camera_world_to_screen(const struct g5_camera *camera,
                                struct g5_world_point world,
                                int32_t *out_x, int32_t *out_y);
/* Expected revision fences stale pointer events after pan/zoom/resize.
 * Rounds persistent world ticks to nearest, halves away from zero. */
bool g5_camera_screen_to_world(const struct g5_camera *camera,
                                uint64_t expected_revision,
                                int32_t x_px, int32_t y_px,
                                struct g5_world_point *out);
/* Conservative broad phase: true if a valid box may touch the viewport,
 * including one pixel of inflation for rounding. No compositor privileges. */
bool g5_camera_aabb_visible(const struct g5_camera *camera,
                             const struct g5_world_aabb *bounds,
                             uint64_t expected_revision,
                             bool *out_visible);
/* Bounded, order-preserving brute-force reference visibility scan.
 * Caller owns output storage. On error no partial output count is published.
 * out_ids may alias neither boxes nor ids. This is not a spatial index. */
bool g5_camera_reference_visible_set(const struct g5_camera *camera,
                                     uint64_t expected_revision,
                                     const uint64_t *ids,
                                     const struct g5_world_aabb *boxes,
                                     size_t count, uint64_t *out_ids,
                                     size_t capacity, size_t *out_count);
#endif
