#ifndef AURORA_GRAPHICS_SURFACE_H
#define AURORA_GRAPHICS_SURFACE_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/graphics_buffer.h>
#include <aurora/spinlock.h>

#define AURORA_GRAPHICS_SURFACE_MAX_OBJECTS 128u
#define AURORA_GRAPHICS_SURFACE_MAX_DAMAGE_RECTS 16u
#define AURORA_GRAPHICS_SURFACE_MAX_FRAME_CALLBACKS 8u

enum aurora_graphics_frame_callback_state {
    AURORA_GRAPHICS_FRAME_CALLBACK_FREE = 0,
    AURORA_GRAPHICS_FRAME_CALLBACK_WAITING,
    AURORA_GRAPHICS_FRAME_CALLBACK_READY
};

struct aurora_graphics_frame_callback {
    uint64_t request_id;
    uint64_t commit_serial;
    uint64_t presentation_serial;
    enum aurora_graphics_frame_callback_state state;
};

struct aurora_graphics_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

enum aurora_graphics_surface_state {
    AURORA_GRAPHICS_SURFACE_FREE = 0,
    AURORA_GRAPHICS_SURFACE_READY,
    AURORA_GRAPHICS_SURFACE_MAPPED
};

struct aurora_graphics_surface_snapshot {
    struct aurora_graphics_buffer *buffer;
    struct aurora_graphics_rect damage[AURORA_GRAPHICS_SURFACE_MAX_DAMAGE_RECTS];
    uint32_t damage_count;
    uint64_t commit_serial;
};

struct aurora_graphics_surface {
    uint64_t object_id;
    uint32_t generation;
    uint32_t owner_refs;
    uint32_t capability_refs;
    bool destroy_requested;
    enum aurora_graphics_surface_state state;
    struct aurora_graphics_surface_snapshot pending;
    struct aurora_graphics_surface_snapshot committed;
    bool pending_frame_callback;
    uint64_t pending_frame_request_id;
    struct aurora_graphics_frame_callback frame_callbacks[
        AURORA_GRAPHICS_SURFACE_MAX_FRAME_CALLBACKS
    ];
};

bool graphics_surface_system_init(void);

struct aurora_graphics_surface *graphics_surface_create(void);

bool graphics_surface_release_owner(
    struct aurora_graphics_surface *surface,
    uint32_t expected_generation
);

aurora_cap_handle graphics_surface_grant(
    struct aurora_cap_table *table,
    struct aurora_graphics_surface *surface,
    uint64_t rights
);

bool graphics_surface_lookup(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    uint64_t required_rights,
    struct aurora_graphics_surface **out_surface
);

bool graphics_surface_attach(
    struct aurora_cap_table *table,
    aurora_cap_handle surface_handle,
    aurora_cap_handle buffer_handle
);

bool graphics_surface_detach_buffers(
    struct aurora_cap_table *table,
    aurora_cap_handle surface_handle
);

bool graphics_surface_damage(
    struct aurora_cap_table *table,
    aurora_cap_handle surface_handle,
    const struct aurora_graphics_rect *rect
);

bool graphics_surface_request_frame_callback(
    struct aurora_cap_table *table,
    aurora_cap_handle surface_handle,
    uint64_t request_id
);

bool graphics_surface_complete_frame(
    struct aurora_graphics_surface *surface,
    uint64_t commit_serial,
    uint64_t presentation_serial
);

bool graphics_surface_take_frame_callback(
    struct aurora_cap_table *table,
    aurora_cap_handle surface_handle,
    struct aurora_graphics_frame_callback *out_callback
);

bool graphics_surface_commit(
    struct aurora_cap_table *table,
    aurora_cap_handle surface_handle,
    uint64_t *out_commit_serial
);

#endif
