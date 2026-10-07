#ifndef AURORA_SOFTWARE_COMPOSITOR_H
#define AURORA_SOFTWARE_COMPOSITOR_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/display_backbuffer.h>
#include <aurora/graphics_surface.h>

#define AURORA_COMPOSITOR_MAX_NODES 64u

struct aurora_compositor_damage {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    bool valid;
};

struct aurora_compositor_node {
    uint64_t node_id;
    aurora_cap_handle surface_handle;
    int32_t x;
    int32_t y;
    int32_t z;
    uint8_t opacity;
    bool visible;
    bool used;
    uint64_t last_commit_serial;
};

struct aurora_software_compositor {
    struct aurora_cap_table surface_caps;
    struct aurora_display_backbuffer backbuffer;
    struct aurora_compositor_node nodes[AURORA_COMPOSITOR_MAX_NODES];
    struct aurora_compositor_damage pending_damage;
    uint32_t output_index;
    uint64_t next_node_id;
    bool initialized;
};

bool software_compositor_init(
    struct aurora_software_compositor *compositor,
    uint32_t output_index
);

bool software_compositor_destroy(
    struct aurora_software_compositor *compositor
);

bool software_compositor_add_surface(
    struct aurora_software_compositor *compositor,
    struct aurora_graphics_surface *surface,
    int32_t x,
    int32_t y,
    int32_t z,
    uint8_t opacity,
    uint64_t *out_node_id
);

bool software_compositor_remove_surface(
    struct aurora_software_compositor *compositor,
    uint64_t node_id
);

bool software_compositor_set_node(
    struct aurora_software_compositor *compositor,
    uint64_t node_id,
    int32_t x,
    int32_t y,
    int32_t z,
    uint8_t opacity,
    bool visible
);

bool software_compositor_compose_present(
    struct aurora_software_compositor *compositor,
    uint64_t *out_present_serial
);

bool software_compositor_selftest(void);

#endif
