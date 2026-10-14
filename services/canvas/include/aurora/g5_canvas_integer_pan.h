#ifndef AURORA_G5_CANVAS_INTEGER_PAN_H
#define AURORA_G5_CANVAS_INTEGER_PAN_H
/* Experimental integer-only translation gate. No floating point, heap, libc,
 * IPC authority or compositor ownership. NOT the full WP-05 camera. */
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#define G5_INTEGER_PAN_MAX_DELTA 32
struct g5_integer_pan {
    int64_t x_ticks, y_ticks;
    uint64_t revision;
};
static inline bool g5_integer_pan_apply(struct g5_integer_pan *state,
                                         int32_t dx_pixels, int32_t dy_pixels,
                                         uint64_t expected_revision) {
    if (!state || !state->revision || state->revision == UINT64_MAX ||
        state->revision != expected_revision ||
        dx_pixels < -G5_INTEGER_PAN_MAX_DELTA ||
        dx_pixels > G5_INTEGER_PAN_MAX_DELTA ||
        dy_pixels < -G5_INTEGER_PAN_MAX_DELTA ||
        dy_pixels > G5_INTEGER_PAN_MAX_DELTA) return false;
    /* At zoom=1, scale=1, one pixel is exactly 1024 world ticks.
     * Checked before mutation; both axes commit atomically. */
    const int64_t dx = (int64_t)dx_pixels * INT64_C(1024);
    const int64_t dy = (int64_t)dy_pixels * INT64_C(1024);
    if ((dx > 0 && state->x_ticks < INT64_MIN + dx) ||
        (dx < 0 && state->x_ticks > INT64_MAX + dx) ||
        (dy > 0 && state->y_ticks < INT64_MIN + dy) ||
        (dy < 0 && state->y_ticks > INT64_MAX + dy)) return false;
    state->x_ticks -= dx;
    state->y_ticks -= dy;
    ++state->revision;
    return true;
}
#endif
