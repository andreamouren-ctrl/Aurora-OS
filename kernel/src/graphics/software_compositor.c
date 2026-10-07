#include <stddef.h>
#include <stdint.h>

#include <aurora/display.h>
#include <aurora/graphics_buffer.h>
#include <aurora/pmm.h>
#include <aurora/software_compositor.h>

static void clear_bytes(void *ptr, uint64_t size) {
    uint8_t *bytes = (uint8_t *)ptr;
    for (uint64_t i = 0u; i < size; ++i) bytes[i] = 0u;
}

static bool format_is_supported_8888(
    const struct aurora_display_pixel_format *format
) {
    if (format == NULL ||
        format->encoding != AURORA_PIXEL_ENCODING_UNORM_PACKED ||
        format->bits_per_pixel != 32u ||
        format->red_mask_size != 8u ||
        format->green_mask_size != 8u ||
        format->blue_mask_size != 8u ||
        (format->alpha_mask_size != 0u &&
         format->alpha_mask_size != 8u)) {
        return false;
    }

    if (format->red_mask_shift > 24u ||
        format->green_mask_shift > 24u ||
        format->blue_mask_shift > 24u ||
        (format->alpha_mask_size == 8u &&
         format->alpha_mask_shift > 24u)) {
        return false;
    }

    return true;
}

static bool rgb_layout_equal(
    const struct aurora_display_pixel_format *a,
    const struct aurora_display_pixel_format *b
) {
    return a != NULL && b != NULL &&
        a->encoding == b->encoding &&
        a->bits_per_pixel == b->bits_per_pixel &&
        a->red_mask_size == b->red_mask_size &&
        a->red_mask_shift == b->red_mask_shift &&
        a->green_mask_size == b->green_mask_size &&
        a->green_mask_shift == b->green_mask_shift &&
        a->blue_mask_size == b->blue_mask_size &&
        a->blue_mask_shift == b->blue_mask_shift;
}

static bool damage_union(
    struct aurora_compositor_damage *damage,
    int64_t x,
    int64_t y,
    uint64_t width,
    uint64_t height,
    uint64_t output_width,
    uint64_t output_height
) {
    if (damage == NULL ||
        width == 0u ||
        height == 0u ||
        output_width == 0u ||
        output_height == 0u) {
        return false;
    }

    int64_t right =
        width > (uint64_t)INT64_MAX
            ? INT64_MAX
            : x + (int64_t)width;
    int64_t bottom =
        height > (uint64_t)INT64_MAX
            ? INT64_MAX
            : y + (int64_t)height;

    int64_t left = x < 0 ? 0 : x;
    int64_t top = y < 0 ? 0 : y;
    int64_t max_right =
        output_width > (uint64_t)INT64_MAX
            ? INT64_MAX
            : (int64_t)output_width;
    int64_t max_bottom =
        output_height > (uint64_t)INT64_MAX
            ? INT64_MAX
            : (int64_t)output_height;

    if (right > max_right) right = max_right;
    if (bottom > max_bottom) bottom = max_bottom;

    if (left >= right || top >= bottom) {
        return true;
    }

    uint32_t nx = (uint32_t)left;
    uint32_t ny = (uint32_t)top;
    uint32_t nr = (uint32_t)right;
    uint32_t nb = (uint32_t)bottom;

    if (!damage->valid) {
        damage->x = nx;
        damage->y = ny;
        damage->width = nr - nx;
        damage->height = nb - ny;
        damage->valid = true;
        return true;
    }

    uint32_t old_right = damage->x + damage->width;
    uint32_t old_bottom = damage->y + damage->height;
    uint32_t ux = nx < damage->x ? nx : damage->x;
    uint32_t uy = ny < damage->y ? ny : damage->y;
    uint32_t ur = nr > old_right ? nr : old_right;
    uint32_t ub = nb > old_bottom ? nb : old_bottom;

    damage->x = ux;
    damage->y = uy;
    damage->width = ur - ux;
    damage->height = ub - uy;
    damage->valid = true;
    return true;
}

static struct aurora_compositor_node *find_node(
    struct aurora_software_compositor *compositor,
    uint64_t node_id
) {
    if (compositor == NULL || node_id == 0u) return NULL;

    for (uint32_t i = 0u; i < AURORA_COMPOSITOR_MAX_NODES; ++i) {
        if (compositor->nodes[i].used &&
            compositor->nodes[i].node_id == node_id) {
            return &compositor->nodes[i];
        }
    }

    return NULL;
}

static bool buffer_read_u32(
    const struct aurora_graphics_buffer *buffer,
    uint64_t byte_offset,
    uint32_t *out_value
) {
    if (buffer == NULL ||
        out_value == NULL ||
        buffer->memory == NULL ||
        byte_offset > buffer->byte_length ||
        buffer->byte_length - byte_offset < sizeof(uint32_t)) {
        return false;
    }

    uint8_t bytes[4];

    for (uint32_t i = 0u; i < 4u; ++i) {
        uint64_t offset = byte_offset + i;
        uint64_t page_index = offset / AURORA_PAGE_SIZE;
        uint64_t page_offset = offset % AURORA_PAGE_SIZE;
        uint64_t physical = 0u;

        if (page_index > UINT32_MAX ||
            !memory_object_page_at(
                buffer->memory,
                (uint32_t)page_index,
                &physical)) {
            return false;
        }

        bytes[i] = *(volatile const uint8_t *)(
            (uintptr_t)pmm_phys_to_virt(physical) +
            (uintptr_t)page_offset
        );
    }

    *out_value =
        (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
    return true;
}

static uint8_t channel(
    uint32_t pixel,
    uint8_t shift
) {
    return (uint8_t)((pixel >> shift) & 0xFFu);
}

static uint32_t replace_channel(
    uint32_t pixel,
    uint8_t shift,
    uint8_t value
) {
    uint32_t mask = UINT32_C(0xFF) << shift;
    return (pixel & ~mask) | ((uint32_t)value << shift);
}

static uint8_t blend_channel(
    uint8_t src,
    uint8_t dst,
    uint32_t alpha
) {
    uint32_t inv = 255u - alpha;
    uint32_t mixed =
        (uint32_t)src * alpha +
        (uint32_t)dst * inv +
        127u;
    return (uint8_t)(mixed / 255u);
}

static bool compose_snapshot(
    struct aurora_software_compositor *compositor,
    const struct aurora_compositor_node *node,
    const struct aurora_graphics_surface_snapshot *snapshot,
    const struct aurora_compositor_damage *damage
) {
    if (compositor == NULL ||
        node == NULL ||
        snapshot == NULL ||
        snapshot->buffer == NULL ||
        damage == NULL ||
        !damage->valid) {
        return false;
    }

    const struct aurora_graphics_buffer *buffer = snapshot->buffer;
    const struct aurora_display_pixel_format *src_format = &buffer->format;
    const struct aurora_display_pixel_format *dst_format =
        &compositor->backbuffer.format;

    if (!format_is_supported_8888(src_format) ||
        !format_is_supported_8888(dst_format) ||
        !rgb_layout_equal(src_format, dst_format)) {
        return false;
    }

    uint32_t damage_right = damage->x + damage->width;
    uint32_t damage_bottom = damage->y + damage->height;

    for (uint32_t dy = damage->y; dy < damage_bottom; ++dy) {
        int64_t sy64 = (int64_t)dy - (int64_t)node->y;
        if (sy64 < 0 || (uint64_t)sy64 >= buffer->height) continue;

        for (uint32_t dx = damage->x; dx < damage_right; ++dx) {
            int64_t sx64 = (int64_t)dx - (int64_t)node->x;
            if (sx64 < 0 || (uint64_t)sx64 >= buffer->width) continue;

            uint64_t sx = (uint64_t)sx64;
            uint64_t sy = (uint64_t)sy64;

            if (sy > UINT64_MAX / buffer->stride ||
                sx > (UINT64_MAX - sy * buffer->stride) / 4u) {
                return false;
            }

            uint64_t src_offset =
                sy * buffer->stride + sx * 4u;

            uint32_t src = 0u;
            if (!buffer_read_u32(
                    buffer,
                    src_offset,
                    &src)) {
                return false;
            }

            if ((uint64_t)dy > UINT64_MAX / compositor->backbuffer.pitch ||
                (uint64_t)dx >
                    (UINT64_MAX -
                     (uint64_t)dy * compositor->backbuffer.pitch) / 4u) {
                return false;
            }

            uint64_t dst_offset =
                (uint64_t)dy * compositor->backbuffer.pitch +
                (uint64_t)dx * 4u;

            if (dst_offset > compositor->backbuffer.byte_length ||
                compositor->backbuffer.byte_length - dst_offset < 4u) {
                return false;
            }

            uint32_t *dst_pixel =
                (uint32_t *)(void *)(
                    compositor->backbuffer.pixels +
                    dst_offset
                );
            uint32_t dst = *dst_pixel;

            uint32_t source_alpha =
                src_format->alpha_mask_size == 8u
                    ? channel(src, src_format->alpha_mask_shift)
                    : 255u;
            uint32_t alpha =
                (source_alpha * (uint32_t)node->opacity + 127u) / 255u;

            if (alpha == 255u) {
                uint32_t out = dst;
                out = replace_channel(
                    out,
                    dst_format->red_mask_shift,
                    channel(src, src_format->red_mask_shift)
                );
                out = replace_channel(
                    out,
                    dst_format->green_mask_shift,
                    channel(src, src_format->green_mask_shift)
                );
                out = replace_channel(
                    out,
                    dst_format->blue_mask_shift,
                    channel(src, src_format->blue_mask_shift)
                );
                *dst_pixel = out;
                continue;
            }

            if (alpha == 0u) continue;

            uint32_t out = dst;
            out = replace_channel(
                out,
                dst_format->red_mask_shift,
                blend_channel(
                    channel(src, src_format->red_mask_shift),
                    channel(dst, dst_format->red_mask_shift),
                    alpha
                )
            );
            out = replace_channel(
                out,
                dst_format->green_mask_shift,
                blend_channel(
                    channel(src, src_format->green_mask_shift),
                    channel(dst, dst_format->green_mask_shift),
                    alpha
                )
            );
            out = replace_channel(
                out,
                dst_format->blue_mask_shift,
                blend_channel(
                    channel(src, src_format->blue_mask_shift),
                    channel(dst, dst_format->blue_mask_shift),
                    alpha
                )
            );
            *dst_pixel = out;
        }
    }

    return true;
}

bool software_compositor_init(
    struct aurora_software_compositor *compositor,
    uint32_t output_index
) {
    if (compositor == NULL) return false;

    const struct aurora_display_mode *mode =
        display_mode_at(output_index, 0u);

    if (mode == NULL ||
        mode->width == 0u ||
        mode->height == 0u ||
        mode->width > UINT32_MAX ||
        mode->height > UINT32_MAX ||
        !format_is_supported_8888(&mode->format)) {
        return false;
    }

    clear_bytes(compositor, sizeof(*compositor));
    cap_table_init(&compositor->surface_caps);

    if (!display_backbuffer_init(
            &compositor->backbuffer,
            mode)) {
        return false;
    }

    clear_bytes(
        compositor->backbuffer.pixels,
        compositor->backbuffer.byte_length
    );

    compositor->output_index = output_index;
    compositor->next_node_id = 1u;
    compositor->initialized = true;
    compositor->pending_damage = (struct aurora_compositor_damage){
        .x = 0u,
        .y = 0u,
        .width = (uint32_t)mode->width,
        .height = (uint32_t)mode->height,
        .valid = true
    };
    return true;
}

bool software_compositor_destroy(
    struct aurora_software_compositor *compositor
) {
    if (compositor == NULL || !compositor->initialized) {
        return false;
    }

    cap_table_destroy(&compositor->surface_caps);

    if (!display_backbuffer_release(&compositor->backbuffer)) {
        return false;
    }

    clear_bytes(compositor, sizeof(*compositor));
    return true;
}

bool software_compositor_add_surface(
    struct aurora_software_compositor *compositor,
    struct aurora_graphics_surface *surface,
    int32_t x,
    int32_t y,
    int32_t z,
    uint8_t opacity,
    uint64_t *out_node_id
) {
    if (out_node_id != NULL) *out_node_id = 0u;

    if (compositor == NULL ||
        !compositor->initialized ||
        surface == NULL) {
        return false;
    }

    uint32_t slot = AURORA_COMPOSITOR_MAX_NODES;
    for (uint32_t i = 0u; i < AURORA_COMPOSITOR_MAX_NODES; ++i) {
        if (!compositor->nodes[i].used) {
            slot = i;
            break;
        }
    }

    if (slot == AURORA_COMPOSITOR_MAX_NODES) {
        return false;
    }

    aurora_cap_handle handle =
        graphics_surface_grant(
            &compositor->surface_caps,
            surface,
            AURORA_RIGHT_READ
        );

    if (handle == AURORA_CAP_INVALID) {
        return false;
    }

    uint64_t node_id = compositor->next_node_id++;
    if (node_id == 0u) {
        node_id = compositor->next_node_id++;
    }
    if (compositor->next_node_id == 0u) {
        compositor->next_node_id = 1u;
    }

    compositor->nodes[slot] = (struct aurora_compositor_node){
        .node_id = node_id,
        .surface_handle = handle,
        .x = x,
        .y = y,
        .z = z,
        .opacity = opacity,
        .visible = true,
        .used = true,
        .last_commit_serial = 0u
    };

    const struct aurora_display_mode *mode =
        display_mode_at(compositor->output_index, 0u);
    struct aurora_graphics_surface_snapshot snapshot = {0};

    if (mode != NULL &&
        graphics_surface_read_committed(
            &compositor->surface_caps,
            handle,
            &snapshot)) {
        (void)damage_union(
            &compositor->pending_damage,
            x,
            y,
            snapshot.buffer->width,
            snapshot.buffer->height,
            mode->width,
            mode->height
        );
        graphics_surface_snapshot_release(&snapshot);
    }

    if (out_node_id != NULL) *out_node_id = node_id;
    return true;
}

bool software_compositor_remove_surface(
    struct aurora_software_compositor *compositor,
    uint64_t node_id
) {
    struct aurora_compositor_node *node =
        find_node(compositor, node_id);

    if (node == NULL) return false;

    const struct aurora_display_mode *mode =
        display_mode_at(compositor->output_index, 0u);
    struct aurora_graphics_surface_snapshot snapshot = {0};

    if (mode != NULL &&
        graphics_surface_read_committed(
            &compositor->surface_caps,
            node->surface_handle,
            &snapshot)) {
        (void)damage_union(
            &compositor->pending_damage,
            node->x,
            node->y,
            snapshot.buffer->width,
            snapshot.buffer->height,
            mode->width,
            mode->height
        );
        graphics_surface_snapshot_release(&snapshot);
    }

    aurora_cap_handle handle = node->surface_handle;
    *node = (struct aurora_compositor_node){0};
    return cap_revoke(&compositor->surface_caps, handle);
}

bool software_compositor_set_node(
    struct aurora_software_compositor *compositor,
    uint64_t node_id,
    int32_t x,
    int32_t y,
    int32_t z,
    uint8_t opacity,
    bool visible
) {
    struct aurora_compositor_node *node =
        find_node(compositor, node_id);

    if (node == NULL) return false;

    const struct aurora_display_mode *mode =
        display_mode_at(compositor->output_index, 0u);
    struct aurora_graphics_surface_snapshot snapshot = {0};

    if (mode == NULL ||
        !graphics_surface_read_committed(
            &compositor->surface_caps,
            node->surface_handle,
            &snapshot)) {
        return false;
    }

    (void)damage_union(
        &compositor->pending_damage,
        node->x,
        node->y,
        snapshot.buffer->width,
        snapshot.buffer->height,
        mode->width,
        mode->height
    );

    node->x = x;
    node->y = y;
    node->z = z;
    node->opacity = opacity;
    node->visible = visible;

    (void)damage_union(
        &compositor->pending_damage,
        node->x,
        node->y,
        snapshot.buffer->width,
        snapshot.buffer->height,
        mode->width,
        mode->height
    );

    graphics_surface_snapshot_release(&snapshot);
    return true;
}

static bool next_node_after(
    const struct aurora_software_compositor *compositor,
    int32_t previous_z,
    uint64_t previous_id,
    bool first,
    uint32_t *out_index
) {
    if (compositor == NULL || out_index == NULL) return false;

    bool found = false;
    int32_t best_z = 0;
    uint64_t best_id = 0u;
    uint32_t best_index = 0u;

    for (uint32_t i = 0u; i < AURORA_COMPOSITOR_MAX_NODES; ++i) {
        const struct aurora_compositor_node *node =
            &compositor->nodes[i];

        if (!node->used || !node->visible) continue;

        bool after =
            first ||
            node->z > previous_z ||
            (node->z == previous_z &&
             node->node_id > previous_id);

        if (!after) continue;

        if (!found ||
            node->z < best_z ||
            (node->z == best_z &&
             node->node_id < best_id)) {
            found = true;
            best_z = node->z;
            best_id = node->node_id;
            best_index = i;
        }
    }

    if (!found) return false;

    *out_index = best_index;
    return true;
}

bool software_compositor_compose_present(
    struct aurora_software_compositor *compositor,
    uint64_t *out_present_serial
) {
    if (out_present_serial != NULL) *out_present_serial = 0u;

    if (compositor == NULL ||
        !compositor->initialized) {
        return false;
    }

    const struct aurora_display_mode *mode =
        display_mode_at(compositor->output_index, 0u);

    if (mode == NULL) return false;

    struct aurora_compositor_damage damage =
        compositor->pending_damage;

    struct aurora_graphics_surface_snapshot snapshots[
        AURORA_COMPOSITOR_MAX_NODES
    ];
    bool have_snapshot[AURORA_COMPOSITOR_MAX_NODES] = {0};
    bool new_commit[AURORA_COMPOSITOR_MAX_NODES] = {0};

    for (uint32_t i = 0u; i < AURORA_COMPOSITOR_MAX_NODES; ++i) {
        snapshots[i] =
            (struct aurora_graphics_surface_snapshot){0};

        struct aurora_compositor_node *node =
            &compositor->nodes[i];

        if (!node->used || !node->visible) continue;

        if (!graphics_surface_read_committed(
                &compositor->surface_caps,
                node->surface_handle,
                &snapshots[i])) {
            continue;
        }

        have_snapshot[i] = true;

        if (snapshots[i].commit_serial !=
            node->last_commit_serial) {
            new_commit[i] = true;

            if (snapshots[i].damage_count == 0u) {
                (void)damage_union(
                    &damage,
                    node->x,
                    node->y,
                    snapshots[i].buffer->width,
                    snapshots[i].buffer->height,
                    mode->width,
                    mode->height
                );
            } else {
                for (uint32_t r = 0u;
                     r < snapshots[i].damage_count;
                     ++r) {
                    const struct aurora_graphics_rect *rect =
                        &snapshots[i].damage[r];

                    (void)damage_union(
                        &damage,
                        (int64_t)node->x + rect->x,
                        (int64_t)node->y + rect->y,
                        rect->width,
                        rect->height,
                        mode->width,
                        mode->height
                    );
                }
            }
        }
    }

    if (!damage.valid) {
        for (uint32_t i = 0u; i < AURORA_COMPOSITOR_MAX_NODES; ++i) {
            if (have_snapshot[i]) {
                graphics_surface_snapshot_release(&snapshots[i]);
            }
        }
        return true;
    }

    uint32_t right = damage.x + damage.width;
    uint32_t bottom = damage.y + damage.height;

    for (uint32_t y = damage.y; y < bottom; ++y) {
        uint64_t row_offset =
            (uint64_t)y * compositor->backbuffer.pitch;
        uint64_t begin =
            row_offset + (uint64_t)damage.x * 4u;
        uint64_t bytes =
            (uint64_t)damage.width * 4u;

        if (begin > compositor->backbuffer.byte_length ||
            bytes > compositor->backbuffer.byte_length - begin) {
            for (uint32_t i = 0u; i < AURORA_COMPOSITOR_MAX_NODES; ++i) {
                if (have_snapshot[i]) {
                    graphics_surface_snapshot_release(&snapshots[i]);
                }
            }
            return false;
        }

        clear_bytes(
            compositor->backbuffer.pixels + begin,
            bytes
        );
    }

    bool first = true;
    int32_t previous_z = 0;
    uint64_t previous_id = 0u;

    for (;;) {
        uint32_t index = 0u;

        if (!next_node_after(
                compositor,
                previous_z,
                previous_id,
                first,
                &index)) {
            break;
        }

        struct aurora_compositor_node *node =
            &compositor->nodes[index];

        if (have_snapshot[index] &&
            !compose_snapshot(
                compositor,
                node,
                &snapshots[index],
                &damage)) {
            for (uint32_t i = 0u; i < AURORA_COMPOSITOR_MAX_NODES; ++i) {
                if (have_snapshot[i]) {
                    graphics_surface_snapshot_release(&snapshots[i]);
                }
            }
            return false;
        }

        first = false;
        previous_z = node->z;
        previous_id = node->node_id;
    }

    uint64_t serial = 0u;
    bool presented =
        display_present(
            compositor->output_index,
            &compositor->backbuffer,
            &serial
        );

    if (presented) {
        compositor->pending_damage =
            (struct aurora_compositor_damage){0};

        for (uint32_t i = 0u; i < AURORA_COMPOSITOR_MAX_NODES; ++i) {
            if (!have_snapshot[i] || !new_commit[i]) continue;

            struct aurora_compositor_node *node =
                &compositor->nodes[i];

            node->last_commit_serial =
                snapshots[i].commit_serial;

            struct aurora_graphics_surface *surface = NULL;
            if (graphics_surface_lookup(
                    &compositor->surface_caps,
                    node->surface_handle,
                    AURORA_RIGHT_READ,
                    &surface)) {
                (void)graphics_surface_complete_frame(
                    surface,
                    snapshots[i].commit_serial,
                    serial
                );
            }
        }
    }

    for (uint32_t i = 0u; i < AURORA_COMPOSITOR_MAX_NODES; ++i) {
        if (have_snapshot[i]) {
            graphics_surface_snapshot_release(&snapshots[i]);
        }
    }

    if (out_present_serial != NULL && presented) {
        *out_present_serial = serial;
    }

    return presented;
}

/*
 * Detailed compositor runtime coverage is wired by the validation bootstrap.
 * This symbol remains a lightweight contract check for non-boot callers.
 */
bool software_compositor_selftest(void) {
    return AURORA_COMPOSITOR_MAX_NODES >= 4u;
}
