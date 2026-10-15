#ifndef AURORA_G5_CANVAS_SCENE_BRIDGE_H
#define AURORA_G5_CANVAS_SCENE_BRIDGE_H
#include <aurora/g5_canvas_session_camera.h>
/* Ring3 adapter: atomic projection of bounded world nodes into a compositor
 * transaction. The callback must publish ALL node positions or none.
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
