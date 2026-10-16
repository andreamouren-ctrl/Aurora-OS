#include <aurora/g5_canvas_scene_bridge.h>

bool g5_canvas_scene_bridge_present(void *context,
    const struct g5_canvas_session_camera *proposed) {
    const struct g5_canvas_scene_bridge *bridge = context;
    int32_t x[64], y[64];
    if (!bridge || !proposed || !bridge->publish || bridge->count > 64u ||
        (bridge->count && !bridge->points)) return false;
    if (!g5_canvas_session_camera_project_nodes(proposed,
        proposed->session_generation, proposed->scene_revision,
        proposed->camera.revision, bridge->points, bridge->count,
        x, y, 64u)) return false;
    return bridge->publish(bridge->context, proposed->session_generation,
        proposed->scene_revision, proposed->camera.revision,
        x, y, bridge->count);
}

bool g5_canvas_scene_receiver_publish(
    void *context, uint64_t session_generation, uint64_t scene_revision,
    uint64_t camera_revision, const int32_t *x, const int32_t *y,
    size_t count) {
    struct g5_canvas_scene_receiver *receiver=context;
    if (!receiver || !receiver->publish || !session_generation ||
        !scene_revision || !camera_revision ||
        receiver->session_generation!=session_generation ||
        receiver->scene_revision!=scene_revision ||
        receiver->camera_revision==UINT64_MAX ||
        camera_revision!=receiver->camera_revision+1u ||
        count>64u || count>receiver->max_nodes ||
        (count && (!x || !y))) return false;
    if (!receiver->publish(receiver->context,session_generation,
        scene_revision,camera_revision,x,y,count)) return false;
    receiver->camera_revision=camera_revision;
    return true;
}
