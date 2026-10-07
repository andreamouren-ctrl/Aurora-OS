#ifndef AURORA_GRAPHICS_SURFACE_H
#define AURORA_GRAPHICS_SURFACE_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/graphics_buffer.h>
#include <aurora/spinlock.h>

#define AURORA_GRAPHICS_SURFACE_MAX_OBJECTS 128u
#define AURORA_GRAPHICS_SURFACE_MAX_DAMAGE_RECTS 16u

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
    enum aurora_graphics_surface_state state;
    struct aurora_graphics_surface_snapshot pending;
    struct aurora_graphics_surface_snapshot committed;
};

bool graphics_surface_system_init(void);

struct aurora_graphics_surface *graphics_surface_create(void);

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

#endif
