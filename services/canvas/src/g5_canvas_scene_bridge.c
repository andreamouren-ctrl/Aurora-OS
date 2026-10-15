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
