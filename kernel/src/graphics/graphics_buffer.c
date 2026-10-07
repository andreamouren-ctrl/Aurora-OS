#include <stddef.h>
#include <stdint.h>

#include <aurora/graphics_buffer.h>
#include <aurora/heap.h>

static struct aurora_graphics_buffer buffers[AURORA_GRAPHICS_BUFFER_MAX_OBJECTS];
static aurora_spinlock buffer_lock = AURORA_SPINLOCK_INIT;
static uint64_t next_object_id;
static bool initialized;

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

    uint8_t *pixels = kheap_alloc((size_t)byte_length, 64u);
    if (pixels == NULL) {
        spinlock_lock(&buffer_lock);
        slot->state = AURORA_GRAPHICS_BUFFER_FREE;
        spinlock_unlock(&buffer_lock);
        return NULL;
    }

    spinlock_lock(&buffer_lock);
    uint64_t object_id = next_object_id++;
    if (next_object_id == 0u) next_object_id = 1u;

    slot->object_id = object_id;
    slot->pixels = pixels;
    slot->width = width;
    slot->height = height;
    slot->stride = stride;
    slot->byte_length = byte_length;
    slot->format = *format;
    spinlock_unlock(&buffer_lock);

    return slot;
}

bool graphics_buffer_destroy(
    struct aurora_graphics_buffer *buffer
) {
    if (!initialized || buffer == NULL) return false;

    spinlock_lock(&buffer_lock);

    if (buffer < &buffers[0] ||
        buffer >= &buffers[AURORA_GRAPHICS_BUFFER_MAX_OBJECTS] ||
        buffer->state == AURORA_GRAPHICS_BUFFER_FREE ||
        buffer->state == AURORA_GRAPHICS_BUFFER_IN_USE ||
        buffer->pixels == NULL ||
        buffer->byte_length == 0u ||
        buffer->byte_length > (uint64_t)SIZE_MAX) {
        spinlock_unlock(&buffer_lock);
        return false;
    }

    uint8_t *pixels = buffer->pixels;
    size_t length = (size_t)buffer->byte_length;
    uint32_t next_generation = buffer->generation + 1u;
    if (next_generation == 0u) next_generation = 1u;

    buffer->pixels = NULL;
    buffer->state = AURORA_GRAPHICS_BUFFER_FREE;
    buffer->generation = next_generation;
    spinlock_unlock(&buffer_lock);

    if (!kheap_free_sized(pixels, length)) {
        return false;
    }

    spinlock_lock(&buffer_lock);
    clear_buffer(buffer);
    buffer->state = AURORA_GRAPHICS_BUFFER_FREE;
    buffer->generation = next_generation;
    spinlock_unlock(&buffer_lock);
    return true;
}

aurora_cap_handle graphics_buffer_grant(
    struct aurora_cap_table *table,
    struct aurora_graphics_buffer *buffer,
    uint64_t rights
) {
    if (table == NULL ||
        buffer == NULL ||
        buffer->state == AURORA_GRAPHICS_BUFFER_FREE ||
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
        buffer->pixels == NULL) {
        return false;
    }

    *out_buffer = buffer;
    return true;
}
