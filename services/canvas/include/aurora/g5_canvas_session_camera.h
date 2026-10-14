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
/* Ring3 session teardown: revoke the adapter before freeing owned surfaces.
 * A zeroed adapter rejects all subsequent stale camera operations. */
void g5_canvas_session_camera_revoke(struct g5_canvas_session_camera *state);
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
/* Serialized scene topology transition. Never changes the camera revision;
 * callers must coordinate the corresponding compositor scene transaction. */
bool g5_canvas_session_camera_advance_scene(
    struct g5_canvas_session_camera *state,
    uint64_t session,uint64_t expected_scene,uint64_t expected_camera);
/* Ring3 pointer routing: reject stale session, scene and camera snapshots
 * before converting a physical pixel to persistent world ticks. */
bool g5_canvas_session_camera_pointer_to_world(
    const struct g5_canvas_session_camera *state,
    uint64_t session,uint64_t scene,uint64_t camera,
    int32_t pixel_x,int32_t pixel_y,struct g5_world_point *world);
/* Project a bounded list of world-space node anchors for a compositor
 * transaction. No surface authority is granted; no output on failure. */
bool g5_canvas_session_camera_project_nodes(
    const struct g5_canvas_session_camera *state,
    uint64_t session,uint64_t scene,uint64_t camera,
    const struct g5_world_point *points,size_t count,
    int32_t *out_x,int32_t *out_y,size_t capacity);
/* Synchronous trusted presenter: true only after atomic presentation.
 * Not an IPC endpoint; caller must serialize scene and session teardown. */
typedef bool (*g5_canvas_session_present_fn)(
    void *context,const struct g5_canvas_session_camera *proposed);
bool g5_canvas_session_camera_present_pan(
    struct g5_canvas_session_camera *current,
    uint64_t session,uint64_t scene,uint64_t camera,
    double dx,double dy,g5_canvas_session_present_fn present,void *context);
bool g5_canvas_session_camera_present_zoom(
    struct g5_canvas_session_camera *current,
    uint64_t session,uint64_t scene,uint64_t camera,
    double factor,double x,double y,
    g5_canvas_session_present_fn present,void *context);
#endif
