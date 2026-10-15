#include <aurora/g5_canvas_session_camera.h>

/* The first Ring3 execution probe is deliberately integer-only.
 * Camera math is linked into the executable but not executed until the
 * scheduler's per-thread SSE/FPU preservation contract is validated.
 * The probe must exit 0 through the real syscall path. */
int64_t g5_canvas_runtime_main(void) {
    struct g5_canvas_session_camera camera = {0};
    g5_canvas_session_camera_revoke(&camera);
    return 0;
}
