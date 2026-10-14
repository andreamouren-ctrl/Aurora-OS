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
/* Staging leaves the live camera untouched until compositor acceptance.
 * A successful presentation must be followed by a serialized commit. */
bool g5_canvas_session_camera_stage_pan(
    const struct g5_canvas_session_camera *current,
    uint64_t session,uint64_t scene,uint64_t camera,
    double dx,double dy,struct g5_canvas_session_camera *staged);
bool g5_canvas_session_camera_stage_zoom(
    const struct g5_canvas_session_camera *current,
    uint64_t session,uint64_t scene,uint64_t camera,
    double factor,double x,double y,
    struct g5_canvas_session_camera *staged);
bool g5_canvas_session_camera_commit(
    struct g5_canvas_session_camera *current,
    const struct g5_canvas_session_camera *staged,
    uint64_t expected_session,uint64_t expected_scene,
    uint64_t expected_camera);
#endif
