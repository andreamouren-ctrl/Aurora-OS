#include <stddef.h>
#include <stdint.h>

#include <aurora/graphics_buffer.h>
#include <aurora/process.h>

static struct aurora_graphics_buffer buffers[AURORA_GRAPHICS_BUFFER_MAX_OBJECTS];
static aurora_spinlock buffer_lock = AURORA_SPINLOCK_INIT;
static uint64_t next_object_id;
static bool initialized;

static void clear_buffer(struct aurora_graphics_buffer *buffer);

static bool buffer_pointer_valid(
    const struct aurora_graphics_buffer *buffer
) {
    return buffer >= &buffers[0] &&
        buffer < &buffers[AURORA_GRAPHICS_BUFFER_MAX_OBJECTS];
}

static void finalize_if_unreferenced(
    struct aurora_graphics_buffer *buffer
) {
    struct aurora_memory_object *memory = NULL;

    spinlock_lock(&buffer_lock);

    if (!buffer_pointer_valid(buffer) ||
        buffer->state == AURORA_GRAPHICS_BUFFER_FREE ||
        !buffer->destroy_requested ||
        buffer->owner_refs != 0u ||
        buffer->capability_refs != 0u ||
        buffer->surface_refs != 0u) {
        spinlock_unlock(&buffer_lock);
        return;
    }

    memory = buffer->memory;
    uint32_t next_generation = buffer->generation + 1u;
    if (next_generation == 0u) next_generation = 1u;

    clear_buffer(buffer);
    buffer->state = AURORA_GRAPHICS_BUFFER_FREE;
    buffer->generation = next_generation;

    spinlock_unlock(&buffer_lock);

    if (memory != NULL) {
        (void)memory_object_release_owner(memory);
    }
}

static bool graphics_buffer_cap_retain(void *object) {
    struct aurora_graphics_buffer *buffer =
        (struct aurora_graphics_buffer *)object;

    spinlock_lock(&buffer_lock);

    if (!buffer_pointer_valid(buffer) ||
        buffer->state == AURORA_GRAPHICS_BUFFER_FREE ||
        buffer->destroy_requested ||
        buffer->capability_refs == UINT32_MAX) {
        spinlock_unlock(&buffer_lock);
        return false;
    }

    ++buffer->capability_refs;
    spinlock_unlock(&buffer_lock);
    return true;
}

static void graphics_buffer_cap_release(void *object) {
    struct aurora_graphics_buffer *buffer =
        (struct aurora_graphics_buffer *)object;

    spinlock_lock(&buffer_lock);

    if (buffer_pointer_valid(buffer) &&
        buffer->state != AURORA_GRAPHICS_BUFFER_FREE &&
        buffer->capability_refs != 0u) {
        --buffer->capability_refs;
    }

    spinlock_unlock(&buffer_lock);
    finalize_if_unreferenced(buffer);
}


static void clear_buffer(struct aurora_graphics_buffer *buffer) {
    uint8_t *bytes = (uint8_t *)buffer;
    for (uint64_t i = 0u; i < sizeof(*buffer); ++i) bytes[i] = 0u;
}

static bool format_valid(const struct aurora_display_pixel_format *format) {
    if (format == NULL ||
        format->bits_per_pixel == 0u ||
        (format->bits_per_pixel % 8u) != 0u ||
        format->bits_per_pixel > 64u) {
        return false;
    }

    return true;
}

bool graphics_buffer_system_init(void) {
    spinlock_init(&buffer_lock);

    if (!cap_lifecycle_register(
            AURORA_CAP_GRAPHICS_BUFFER,
            graphics_buffer_cap_retain,
            graphics_buffer_cap_release)) {
        return false;
    }
    for (uint32_t i = 0u; i < AURORA_GRAPHICS_BUFFER_MAX_OBJECTS; ++i) {
        clear_buffer(&buffers[i]);
        buffers[i].state = AURORA_GRAPHICS_BUFFER_FREE;
        buffers[i].generation = 1u;
    }
    next_object_id = 1u;
    initialized = true;
    return true;
}

struct aurora_graphics_buffer *graphics_buffer_create(
    uint64_t width,
    uint64_t height,
    const struct aurora_display_pixel_format *format
) {
    if (!initialized ||
        width == 0u ||
        height == 0u ||
        width > AURORA_GRAPHICS_BUFFER_MAX_DIMENSION ||
        height > AURORA_GRAPHICS_BUFFER_MAX_DIMENSION ||
        !format_valid(format)) {
        return NULL;
    }

    uint64_t bytes_per_pixel = (uint64_t)format->bits_per_pixel / 8u;
    if (width > UINT64_MAX / bytes_per_pixel) return NULL;

    uint64_t stride = width * bytes_per_pixel;
    if (height > UINT64_MAX / stride) return NULL;

    uint64_t byte_length = height * stride;
    if (byte_length == 0u ||
        byte_length > AURORA_GRAPHICS_BUFFER_MAX_BYTES ||
        byte_length > (uint64_t)SIZE_MAX) {
        return NULL;
    }

    spinlock_lock(&buffer_lock);

    struct aurora_graphics_buffer *slot = NULL;
    for (uint32_t i = 0u; i < AURORA_GRAPHICS_BUFFER_MAX_OBJECTS; ++i) {
        if (buffers[i].state == AURORA_GRAPHICS_BUFFER_FREE) {
            slot = &buffers[i];
            break;
        }
    }

    if (slot == NULL) {
        spinlock_unlock(&buffer_lock);
        return NULL;
    }

    uint32_t generation = slot->generation == 0u ? 1u : slot->generation;
    clear_buffer(slot);
    slot->generation = generation;
    slot->state = AURORA_GRAPHICS_BUFFER_READY;
    spinlock_unlock(&buffer_lock);

    uint64_t rounded =
        (byte_length + AURORA_PAGE_SIZE - 1u) &
        ~(AURORA_PAGE_SIZE - 1u);

    if (rounded < byte_length ||
        rounded / AURORA_PAGE_SIZE > UINT32_MAX) {
        spinlock_lock(&buffer_lock);
        slot->state = AURORA_GRAPHICS_BUFFER_FREE;
        spinlock_unlock(&buffer_lock);
        return NULL;
    }

    uint32_t page_count =
        (uint32_t)(rounded / AURORA_PAGE_SIZE);

    struct aurora_memory_object *memory =
        memory_object_create(page_count);

    if (memory == NULL) {
        spinlock_lock(&buffer_lock);
        slot->state = AURORA_GRAPHICS_BUFFER_FREE;
        spinlock_unlock(&buffer_lock);
        return NULL;
    }

    spinlock_lock(&buffer_lock);
    uint64_t object_id = next_object_id++;
    if (next_object_id == 0u) next_object_id = 1u;

    slot->object_id = object_id;
    slot->memory = memory;
    slot->owner_refs = 1u;
    slot->capability_refs = 0u;
    slot->surface_refs = 0u;
    slot->destroy_requested = false;
    slot->width = width;
    slot->height = height;
    slot->stride = stride;
    slot->byte_length = byte_length;
    slot->format = *format;
    spinlock_unlock(&buffer_lock);

    return slot;
}

bool graphics_buffer_release_owner(
    struct aurora_graphics_buffer *buffer,
    uint32_t expected_generation
) {
    if (buffer == NULL) return false;

    spinlock_lock(&buffer_lock);

    if (!buffer_pointer_valid(buffer) ||
        buffer->state == AURORA_GRAPHICS_BUFFER_FREE ||
        buffer->generation != expected_generation ||
        buffer->owner_refs == 0u) {
        spinlock_unlock(&buffer_lock);
        return false;
    }

    --buffer->owner_refs;
    buffer->destroy_requested = true;

    spinlock_unlock(&buffer_lock);

    finalize_if_unreferenced(buffer);
    return true;
}

bool graphics_buffer_retain_surface(
    struct aurora_graphics_buffer *buffer
) {
    if (buffer == NULL) return false;

    spinlock_lock(&buffer_lock);

    if (!buffer_pointer_valid(buffer) ||
        buffer->state == AURORA_GRAPHICS_BUFFER_FREE ||
        buffer->destroy_requested ||
        buffer->surface_refs == UINT32_MAX) {
        spinlock_unlock(&buffer_lock);
        return false;
    }

    ++buffer->surface_refs;
    spinlock_unlock(&buffer_lock);
    return true;
}

void graphics_buffer_release_surface(
    struct aurora_graphics_buffer *buffer
) {
    if (buffer == NULL) return;

    spinlock_lock(&buffer_lock);

    if (buffer_pointer_valid(buffer) &&
        buffer->state != AURORA_GRAPHICS_BUFFER_FREE &&
        buffer->surface_refs != 0u) {
        --buffer->surface_refs;
    }

    spinlock_unlock(&buffer_lock);
    finalize_if_unreferenced(buffer);
}

aurora_cap_handle graphics_buffer_grant(
    struct aurora_cap_table *table,
    struct aurora_graphics_buffer *buffer,
    uint64_t rights
) {
    if (table == NULL ||
        buffer == NULL ||
        buffer->state == AURORA_GRAPHICS_BUFFER_FREE ||
        buffer->destroy_requested ||
        (rights & ~(AURORA_RIGHT_READ |
                    AURORA_RIGHT_WRITE |
                    AURORA_RIGHT_MAP |
                    AURORA_RIGHT_TRANSFER)) != 0u) {
        return AURORA_CAP_INVALID;
    }

    return cap_grant(
        table,
        buffer,
        AURORA_CAP_GRAPHICS_BUFFER,
        rights
    );
}

bool graphics_buffer_lookup(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    uint64_t required_rights,
    struct aurora_graphics_buffer **out_buffer
) {
    if (out_buffer == NULL) return false;
    *out_buffer = NULL;

    struct aurora_capability_view view;
    if (!cap_lookup(
            table,
            handle,
            AURORA_CAP_GRAPHICS_BUFFER,
            required_rights,
            &view)) {
        return false;
    }

    struct aurora_graphics_buffer *buffer =
        (struct aurora_graphics_buffer *)view.object;

    if (buffer < &buffers[0] ||
        buffer >= &buffers[AURORA_GRAPHICS_BUFFER_MAX_OBJECTS] ||
        buffer->state == AURORA_GRAPHICS_BUFFER_FREE ||
        buffer->memory == NULL) {
        return false;
    }

    *out_buffer = buffer;
    return true;
}

bool graphics_buffer_lookup_retain(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    uint64_t required_rights,
    struct aurora_graphics_buffer **out_buffer,
    struct aurora_capability_view *out_view
) {
    if (out_buffer == NULL || out_view == NULL) {
        return false;
    }

    *out_buffer = NULL;
    out_view->object = NULL;
    out_view->type = AURORA_CAP_NONE;
    out_view->rights = 0u;

    if (!cap_lookup_retain(
            table,
            handle,
            AURORA_CAP_GRAPHICS_BUFFER,
            required_rights,
            out_view)) {
        return false;
    }

    struct aurora_graphics_buffer *buffer =
        (struct aurora_graphics_buffer *)out_view->object;

    spinlock_lock(&buffer_lock);
    bool valid =
        buffer_pointer_valid(buffer) &&
        buffer->state != AURORA_GRAPHICS_BUFFER_FREE &&
        buffer->memory != NULL;
    spinlock_unlock(&buffer_lock);

    if (!valid) {
        cap_view_release(out_view);
        out_view->object = NULL;
        out_view->type = AURORA_CAP_NONE;
        out_view->rights = 0u;
        return false;
    }

    *out_buffer = buffer;
    return true;
}

bool graphics_buffer_map_process(
    struct aurora_process *process,
    aurora_cap_handle handle,
    bool writable,
    uint64_t *out_address
) {
    if (process == NULL || out_address == NULL) {
        return false;
    }

    uint64_t required =
        AURORA_RIGHT_MAP |
        (writable ? AURORA_RIGHT_WRITE : AURORA_RIGHT_READ);

    struct aurora_graphics_buffer *buffer = NULL;
    struct aurora_capability_view view = {0};

    if (!graphics_buffer_lookup_retain(
            &process->capabilities,
            handle,
            required,
            &buffer,
            &view)) {
        return false;
    }

    struct aurora_memory_object *memory = NULL;

    spinlock_lock(&buffer_lock);
    memory = buffer->memory;
    spinlock_unlock(&buffer_lock);

    bool mapped =
        memory != NULL &&
        process_shared_memory_map(
            process,
            memory,
            writable,
            out_address
        );

    cap_view_release(&view);
    return mapped;
}

bool graphics_buffer_unmap_process(
    struct aurora_process *process,
    uint64_t address
) {
    return process_shared_memory_unmap(
        process,
        address
    );
}
