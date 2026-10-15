#ifndef AURORA_G5_CANVAS_SCENE_BRIDGE_H
#define AURORA_G5_CANVAS_SCENE_BRIDGE_H
#include <aurora/g5_canvas_session_camera.h>
/* Ring3 adapter: bounded world-node projection into a compositor
 * transaction. The callback must publish ALL node positions or none,
 * validate the session/scene/camera revisions and the receiving surface
 * ownership, and return true only after the whole batch is accepted.
 * A successful callback is externally visible and cannot be rolled back:
 * callers MUST hold an exclusive session/scene lock through presentation
 * and camera commit (use the session_camera_locked_* entry points).
 * This interface alone does not implement compositor IPC or authority.
 * No kernel camera state or floating point crosses this interface. */
typedef bool (*g5_canvas_scene_publish_fn)(
    void *context, uint64_t session_generation, uint64_t scene_revision,
    uint64_t camera_revision, const int32_t *x, const int32_t *y,
    size_t count);
struct g5_canvas_scene_bridge {
    const struct g5_world_point *points;
    size_t count;
    g5_canvas_scene_publish_fn publish;
    void *context;
};
bool g5_canvas_scene_bridge_present(
    void *context, const struct g5_canvas_session_camera *proposed);
#endif
