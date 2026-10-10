#ifndef AURORA_G5_SHELL_SCENE_H
#define AURORA_G5_SHELL_SCENE_H

#include <stdbool.h>
#include <stdint.h>
#include <aurora/g5_compositor_bridge.h>
#include <aurora/g5_ipc_abi.h>
#include <aurora/graphics_buffer.h>
#include <aurora/process.h>
#include <aurora/window_policy.h>
#include <aurora/graphics_input_router.h>

#define G5_SHELL_SCENE_WIDTH 160u
#define G5_SHELL_SCENE_HEIGHT 96u

/* Per-authenticated-session scene, owned by the trusted receiver.
 * Ring3 owns only non-transferable buffer and surface capabilities. */
/* Additional independently owned client sharing the Shell's compositor. */
struct g5_shell_extra_client {
    struct aurora_process *owner;
    struct aurora_graphics_buffer *buffer;
    struct aurora_graphics_surface *surface;
    aurora_cap_handle kernel_surface;
    aurora_cap_handle user_buffer;
    aurora_cap_handle user_surface;
    uint64_t window_id;
    uint64_t configure_serial;
    uint32_t slot;
    bool active;
};

struct g5_shell_scene {
    struct aurora_process *owner;
    struct aurora_graphics_buffer *buffer;
    struct aurora_graphics_surface *surface;
    struct aurora_cap_table kernel_caps;
    aurora_cap_handle kernel_surface;
    aurora_cap_handle user_buffer;
    aurora_cap_handle user_surface;
    struct g5_frame_submission frame;
    struct g5_frame_delivery delivery;
    struct g5_compositor_bridge bridge;
    struct aurora_software_compositor compositor;
    struct aurora_window_policy window_policy;
    struct aurora_graphics_input_router input_router;
    uint64_t window_id;
    struct g5_shell_extra_client extra;
    uint64_t generation;
    uint64_t configure_serial;
    uint64_t last_display_serial;
    uint32_t slot;
    int32_t x;
    int32_t y;
    bool active;
};

bool g5_shell_scene_begin(struct g5_shell_scene *scene,
                          struct aurora_process *owner,uint64_t generation);
bool g5_shell_scene_publish(struct g5_shell_scene *scene,
                            const struct g5_ipc_header *header,
                            const uint8_t *payload);
bool g5_shell_scene_place(struct g5_shell_scene *scene,
                          const struct g5_ipc_header *header,
                          const uint8_t *payload);
bool g5_shell_scene_attach_second(
    struct g5_shell_scene *scene,
    struct aurora_process *second_owner
);
void g5_shell_scene_detach_second(struct g5_shell_scene *scene);
/* Trusted per-client receiver must authenticate process ownership before
 * calling this; it is not a globally exposed Ring 3 IPC dispatcher. */
bool g5_shell_scene_publish_second(
    struct g5_shell_scene *scene,
    struct aurora_process *sender,
    uint64_t request_id,
    uint64_t commit_serial,
    uint64_t *out_display_serial
);
bool g5_shell_scene_close(struct g5_shell_scene *scene,
                          const struct g5_ipc_header *header,
                          const uint8_t *payload);
void g5_shell_scene_end(struct g5_shell_scene *scene);
#endif
