#ifndef AURORA_GRAPHICS_BUFFER_H
#define AURORA_GRAPHICS_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/display_output.h>
#include <aurora/memory_object.h>
#include <aurora/spinlock.h>

struct aurora_process;

#define AURORA_GRAPHICS_BUFFER_MAX_OBJECTS 64u
#define AURORA_GRAPHICS_BUFFER_MAX_DIMENSION 8192u
#define AURORA_GRAPHICS_BUFFER_MAX_BYTES (64ull * 1024ull * 1024ull)

enum aurora_graphics_buffer_state {
    AURORA_GRAPHICS_BUFFER_FREE = 0,
    AURORA_GRAPHICS_BUFFER_READY,
    AURORA_GRAPHICS_BUFFER_COMMITTED,
    AURORA_GRAPHICS_BUFFER_IN_USE,
    AURORA_GRAPHICS_BUFFER_RELEASED
};

struct aurora_graphics_buffer {
    uint64_t object_id;
    uint32_t generation;
    enum aurora_graphics_buffer_state state;
    uint32_t owner_refs;
    uint32_t capability_refs;
    uint32_t surface_refs;
    bool destroy_requested;
    struct aurora_memory_object *memory;
    uint64_t width;
    uint64_t height;
    uint64_t stride;
    uint64_t byte_length;
    struct aurora_display_pixel_format format;
};

bool graphics_buffer_system_init(void);

bool graphics_buffer_release_owner(
    struct aurora_graphics_buffer *buffer
);

bool graphics_buffer_retain_surface(
    struct aurora_graphics_buffer *buffer
);

void graphics_buffer_release_surface(
    struct aurora_graphics_buffer *buffer
);

struct aurora_graphics_buffer *graphics_buffer_create(
    uint64_t width,
    uint64_t height,
    const struct aurora_display_pixel_format *format
);

aurora_cap_handle graphics_buffer_grant(
    struct aurora_cap_table *table,
    struct aurora_graphics_buffer *buffer,
    uint64_t rights
);

bool graphics_buffer_map_process(
    struct aurora_process *process,
    aurora_cap_handle handle,
    bool writable,
    uint64_t *out_address
);

bool graphics_buffer_unmap_process(
    struct aurora_process *process,
    uint64_t address
);

bool graphics_buffer_lookup(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    uint64_t required_rights,
    struct aurora_graphics_buffer **out_buffer
);

bool graphics_buffer_lookup_retain(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    uint64_t required_rights,
    struct aurora_graphics_buffer **out_buffer,
    struct aurora_capability_view *out_view
);

#endif
