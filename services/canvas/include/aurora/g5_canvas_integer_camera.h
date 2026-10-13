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
#endif
