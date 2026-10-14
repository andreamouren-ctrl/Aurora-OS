#ifndef AURORA_G5_CANVAS_SESSION_CAMERA_H
#define AURORA_G5_CANVAS_SESSION_CAMERA_H
/* Ring 3 Canvas Engine adapter. No kernel include, no IPC authority grant.
 * Camera revision and scene topology revision are independent. */
#include <aurora/g5_canvas_camera.h>
struct g5_canvas_session_camera {
    struct g5_camera camera;
    uint64_t session_generation;
    uint64_t scene_revision;
};
bool g5_canvas_session_camera_init(struct g5_canvas_session_camera *state,
    uint64_t session_generation,uint64_t scene_revision,
    struct g5_world_point origin,uint32_t width,uint32_t height);
bool g5_canvas_session_camera_pan(struct g5_canvas_session_camera *state,
    uint64_t session_generation,uint64_t expected_scene_revision,
    uint64_t expected_camera_revision,double screen_dx,double screen_dy);
bool g5_canvas_session_camera_zoom(struct g5_canvas_session_camera *state,
    uint64_t session_generation,uint64_t expected_scene_revision,
    uint64_t expected_camera_revision,double factor,double pointer_x,
    double pointer_y);
#endif
