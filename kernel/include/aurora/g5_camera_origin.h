#ifndef AURORA_G5_CAMERA_ORIGIN_H
#define AURORA_G5_CAMERA_ORIGIN_H
/* Kernel-safe bounded translation: no FPU, heap or libc. */
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
static inline bool g5_camera_origin_stage(int64_t x, int64_t y,
                                           int64_t dx, int64_t dy,
                                           int64_t *next_x,int64_t *next_y) {
    if (!next_x || !next_y || next_x==next_y ||
        (dx==0 && dy==0) || dx < -32 || dx > 32 ||
        dy < -32 || dy > 32) return false;
    const int64_t tx=dx*INT64_C(1024),ty=dy*INT64_C(1024);
    if ((tx>0 && x<INT64_MIN+tx) ||
        (tx<0 && x>INT64_MAX+tx) ||
        (ty>0 && y<INT64_MIN+ty) ||
        (ty<0 && y>INT64_MAX+ty)) return false;
    *next_x=x-tx;
    *next_y=y-ty;
    return true;
}
#endif
