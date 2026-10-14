#ifndef AURORA_G5_CANVAS_PAN_FRAME_H
#define AURORA_G5_CANVAS_PAN_FRAME_H
/* Explicit translation adapter for existing +screen dx/dy pan IPC.
 * World camera origin moves by -dx/-dy at zoom=scale=1.
 * This is a host prototype, NOT an IPC decoder or compositor transaction. */
#include <aurora/g5_canvas_integer_pan.h>
static inline bool g5_canvas_pan_screen_to_world(
    struct g5_integer_pan *camera, int32_t screen_dx, int32_t screen_dy,
    uint64_t expected_revision) {
    /* Negation is safe because the bounded input is checked first. */
    if (screen_dx < -G5_INTEGER_PAN_MAX_DELTA ||
        screen_dx > G5_INTEGER_PAN_MAX_DELTA ||
        screen_dy < -G5_INTEGER_PAN_MAX_DELTA ||
        screen_dy > G5_INTEGER_PAN_MAX_DELTA) return false;
    return g5_integer_pan_apply(camera,screen_dx,screen_dy,
                                expected_revision);
}
#endif
