#include <stddef.h>
#include <stdint.h>

#include <aurora/display_backbuffer.h>
#include <aurora/heap.h>

static void clear_backbuffer(
    struct aurora_display_backbuffer *buffer
) {
    uint8_t *bytes = (uint8_t *)buffer;

    for (uint64_t i = 0u; i < sizeof(*buffer); ++i) {
        bytes[i] = 0u;
    }
}

bool display_backbuffer_init(
    struct aurora_display_backbuffer *buffer,
    const struct aurora_display_mode *mode
) {
    if (buffer == NULL ||
        !display_mode_valid(mode) ||
        mode->height > UINT64_MAX / mode->pitch) {
        return false;
    }

    uint64_t byte_length = mode->height * mode->pitch;

    if (byte_length == 0u ||
        byte_length > (uint64_t)SIZE_MAX) {
        return false;
    }

    clear_backbuffer(buffer);

    uint8_t *pixels = kheap_alloc(
        (size_t)byte_length,
        64u
    );

    if (pixels == NULL) {
        return false;
    }

    buffer->pixels = pixels;
    buffer->width = mode->width;
    buffer->height = mode->height;
    buffer->pitch = mode->pitch;
    buffer->byte_length = byte_length;
    buffer->format = mode->format;
    buffer->generation = 1u;
    buffer->ready = true;
    buffer->in_flight = false;
    return true;
}

bool display_backbuffer_release(
    struct aurora_display_backbuffer *buffer
) {
    if (buffer == NULL ||
        !buffer->ready ||
        buffer->in_flight ||
        buffer->pixels == NULL ||
        buffer->byte_length == 0u ||
        buffer->byte_length > (uint64_t)SIZE_MAX) {
        return false;
    }

    if (!kheap_free_sized(
            buffer->pixels,
            (size_t)buffer->byte_length)) {
        return false;
    }

    clear_backbuffer(buffer);
    return true;
}
