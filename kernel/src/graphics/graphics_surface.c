#include <stddef.h>
#include <stdint.h>

#include <aurora/graphics_surface.h>

static struct aurora_graphics_surface surfaces[AURORA_GRAPHICS_SURFACE_MAX_OBJECTS];
static aurora_spinlock surface_lock = AURORA_SPINLOCK_INIT;
static uint64_t next_surface_id;
static uint64_t next_commit_serial;
static bool initialized;

static void clear_surface(struct aurora_graphics_surface *surface) {
    uint8_t *bytes = (uint8_t *)surface;
    for (uint64_t i = 0u; i < sizeof(*surface); ++i) bytes[i] = 0u;
}

bool graphics_surface_system_init(void) {
    spinlock_init(&surface_lock);

    for (uint32_t i = 0u; i < AURORA_GRAPHICS_SURFACE_MAX_OBJECTS; ++i) {
        clear_surface(&surfaces[i]);
        surfaces[i].state = AURORA_GRAPHICS_SURFACE_FREE;
    }

    next_surface_id = 1u;
    next_commit_serial = 1u;
    initialized = true;
    return true;
}

struct aurora_graphics_surface *graphics_surface_create(void) {
    if (!initialized) return NULL;

    spinlock_lock(&surface_lock);

    struct aurora_graphics_surface *slot = NULL;

    for (uint32_t i = 0u; i < AURORA_GRAPHICS_SURFACE_MAX_OBJECTS; ++i) {
        if (surfaces[i].state == AURORA_GRAPHICS_SURFACE_FREE) {
            slot = &surfaces[i];
            break;
        }
    }

    if (slot == NULL) {
        spinlock_unlock(&surface_lock);
        return NULL;
    }

    clear_surface(slot);
    slot->object_id = next_surface_id++;
    if (next_surface_id == 0u) next_surface_id = 1u;
    slot->state = AURORA_GRAPHICS_SURFACE_READY;

    spinlock_unlock(&surface_lock);
    return slot;
}

aurora_cap_handle graphics_surface_grant(
    struct aurora_cap_table *table,
    struct aurora_graphics_surface *surface,
    uint64_t rights
) {
    if (table == NULL ||
        surface == NULL ||
        surface->state == AURORA_GRAPHICS_SURFACE_FREE ||
        (rights & ~(AURORA_RIGHT_READ |
                    AURORA_RIGHT_WRITE |
                    AURORA_RIGHT_CONTROL |
                    AURORA_RIGHT_TRANSFER)) != 0u) {
        return AURORA_CAP_INVALID;
    }

    return cap_grant(
        table,
        surface,
        AURORA_CAP_SURFACE,
        rights
    );
}

bool graphics_surface_lookup(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    uint64_t required_rights,
    struct aurora_graphics_surface **out_surface
) {
    if (out_surface == NULL) return false;
    *out_surface = NULL;

    struct aurora_capability_view view;

    if (!cap_lookup(
            table,
            handle,
            AURORA_CAP_SURFACE,
            required_rights,
            &view)) {
        return false;
    }

    struct aurora_graphics_surface *surface =
        (struct aurora_graphics_surface *)view.object;

    if (surface < &surfaces[0] ||
        surface >= &surfaces[AURORA_GRAPHICS_SURFACE_MAX_OBJECTS] ||
        surface->state == AURORA_GRAPHICS_SURFACE_FREE) {
        return false;
    }

    *out_surface = surface;
    return true;
}

static bool rect_within_buffer(
    const struct aurora_graphics_rect *rect,
    const struct aurora_graphics_buffer *buffer
) {
    if (rect == NULL ||
        buffer == NULL ||
        rect->width == 0u ||
        rect->height == 0u) {
        return false;
    }

    uint64_t x_end = (uint64_t)rect->x + (uint64_t)rect->width;
    uint64_t y_end = (uint64_t)rect->y + (uint64_t)rect->height;

    return x_end <= buffer->width &&
        y_end <= buffer->height;
}

bool graphics_surface_attach(
    struct aurora_cap_table *table,
    aurora_cap_handle surface_handle,
    aurora_cap_handle buffer_handle
) {
    struct aurora_graphics_surface *surface = NULL;
    struct aurora_graphics_buffer *buffer = NULL;

    if (!graphics_surface_lookup(
            table,
            surface_handle,
            AURORA_RIGHT_WRITE,
            &surface) ||
        !graphics_buffer_lookup(
            table,
            buffer_handle,
            AURORA_RIGHT_READ,
            &buffer)) {
        return false;
    }

    spinlock_lock(&surface_lock);

    if (surface->state == AURORA_GRAPHICS_SURFACE_FREE ||
        buffer->state == AURORA_GRAPHICS_BUFFER_FREE ||
        buffer->memory == NULL) {
        spinlock_unlock(&surface_lock);
        return false;
    }

    surface->pending.buffer = buffer;
    surface->pending.damage_count = 0u;
    surface->pending.commit_serial = 0u;

    spinlock_unlock(&surface_lock);
    return true;
}

bool graphics_surface_damage(
    struct aurora_cap_table *table,
    aurora_cap_handle surface_handle,
    const struct aurora_graphics_rect *rect
) {
    struct aurora_graphics_surface *surface = NULL;

    if (!graphics_surface_lookup(
            table,
            surface_handle,
            AURORA_RIGHT_WRITE,
            &surface)) {
        return false;
    }

    spinlock_lock(&surface_lock);

    struct aurora_graphics_buffer *buffer =
        surface->pending.buffer;

    if (buffer == NULL ||
        surface->pending.damage_count >=
            AURORA_GRAPHICS_SURFACE_MAX_DAMAGE_RECTS ||
        !rect_within_buffer(rect, buffer)) {
        spinlock_unlock(&surface_lock);
        return false;
    }

    surface->pending.damage[
        surface->pending.damage_count++
    ] = *rect;

    spinlock_unlock(&surface_lock);
    return true;
}

bool graphics_surface_commit(
    struct aurora_cap_table *table,
    aurora_cap_handle surface_handle,
    uint64_t *out_commit_serial
) {
    if (out_commit_serial != NULL) {
        *out_commit_serial = 0u;
    }

    struct aurora_graphics_surface *surface = NULL;

    if (!graphics_surface_lookup(
            table,
            surface_handle,
            AURORA_RIGHT_WRITE,
            &surface)) {
        return false;
    }

    spinlock_lock(&surface_lock);

    struct aurora_graphics_buffer *buffer =
        surface->pending.buffer;

    if (buffer == NULL ||
        buffer->state == AURORA_GRAPHICS_BUFFER_FREE ||
        buffer->memory == NULL) {
        spinlock_unlock(&surface_lock);
        return false;
    }

    /*
     * A newly attached buffer must describe at least one valid damaged area.
     * A zero-damage commit is allowed only when re-committing the exact
     * already-visible buffer.
     */
    if (surface->pending.damage_count == 0u &&
        surface->committed.buffer != buffer) {
        spinlock_unlock(&surface_lock);
        return false;
    }

    for (uint32_t i = 0u;
         i < surface->pending.damage_count;
         ++i) {
        if (!rect_within_buffer(
                &surface->pending.damage[i],
                buffer)) {
            spinlock_unlock(&surface_lock);
            return false;
        }
    }

    uint64_t serial = next_commit_serial++;
    if (serial == 0u) {
        serial = next_commit_serial++;
    }
    if (next_commit_serial == 0u) {
        next_commit_serial = 1u;
    }

    struct aurora_graphics_surface_snapshot committed =
        surface->pending;
    committed.commit_serial = serial;

    /*
     * Publication is one structure assignment while holding the surface lock:
     * readers can never observe a partially promoted pending state.
     */
    surface->committed = committed;
    surface->state = AURORA_GRAPHICS_SURFACE_MAPPED;
    buffer->state = AURORA_GRAPHICS_BUFFER_COMMITTED;

    surface->pending.damage_count = 0u;
    surface->pending.commit_serial = 0u;

    spinlock_unlock(&surface_lock);

    if (out_commit_serial != NULL) {
        *out_commit_serial = serial;
    }

    return true;
}
