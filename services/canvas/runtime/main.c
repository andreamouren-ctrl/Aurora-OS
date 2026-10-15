#include <aurora/g5_canvas_session_camera.h>

/* Bootstrap stays inactive until a session endpoint is delegated. */
int64_t g5_canvas_runtime_main(void) {
    struct g5_canvas_session_camera camera = {0};
    g5_canvas_session_camera_revoke(&camera);
    return 0;
}
