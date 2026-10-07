#include <stddef.h>
#include <stdint.h>

#include <aurora/graphics_surface.h>

static struct aurora_graphics_surface surfaces[AURORA_GRAPHICS_SURFACE_MAX_OBJECTS];
static aurora_spinlock surface_lock = AURORA_SPINLOCK_INIT;
static uint64_t next_surface_id;
static uint64_t next_commit_serial;
static bool initialized;

static bool surface_pointer_valid(
    const struct aurora_graphics_surface *surface
) {
    return surface >= &surfaces[0] &&
        surface < &surfaces[AURORA_GRAPHICS_SURFACE_MAX_OBJECTS];
}

static void clear_surface(struct aurora_graphics_surface *surface) {
    uint8_t *bytes = (uint8_t *)surface;
    for (uint64_t i = 0u; i < sizeof(*surface); ++i) bytes[i] = 0u;
}

static void cancel_frame_callbacks_locked(
    struct aurora_graphics_surface *surface
) {
    surface->pending_frame_callback = false;
    surface->pending_frame_request_id = 0u;

    for (uint32_t i = 0u;
         i < AURORA_GRAPHICS_SURFACE_MAX_FRAME_CALLBACKS;
         ++i) {
        surface->frame_callbacks[i].request_id = 0u;
        surface->frame_callbacks[i].commit_serial = 0u;
        surface->frame_callbacks[i].presentation_serial = 0u;
        surface->frame_callbacks[i].state =
            AURORA_GRAPHICS_FRAME_CALLBACK_FREE;
    }
}

static void finalize_if_unreferenced(
    struct aurora_graphics_surface *surface
) {
    struct aurora_graphics_buffer *pending = NULL;
    struct aurora_graphics_buffer *committed = NULL;

    spinlock_lock(&surface_lock);

    if (!surface_pointer_valid(surface) ||
        surface->state == AURORA_GRAPHICS_SURFACE_FREE ||
        !surface->destroy_requested ||
        surface->owner_refs != 0u ||
        surface->capability_refs != 0u) {
        spinlock_unlock(&surface_lock);
        return;
    }

    pending = surface->pending.buffer;
    committed = surface->committed.buffer;

    /*
     * Final destruction is the cancellation boundary for frame callbacks.
     * Once no capability references remain there is no recipient left, so
     * pending, waiting and ready callbacks are dropped deterministically and
     * cannot leak into a recycled surface slot.
     */
    cancel_frame_callbacks_locked(surface);

    uint32_t next_generation = surface->generation + 1u;
    if (next_generation == 0u) next_generation = 1u;

    clear_surface(surface);
    surface->state = AURORA_GRAPHICS_SURFACE_FREE;
    surface->generation = next_generation;

    spinlock_unlock(&surface_lock);

    if (pending != NULL) {
        graphics_buffer_release_surface(pending);
    }

    if (committed != NULL) {
        graphics_buffer_release_surface(committed);
    }
}

static bool graphics_surface_cap_retain(void *object) {
    struct aurora_graphics_surface *surface =
        (struct aurora_graphics_surface *)object;

    spinlock_lock(&surface_lock);

    if (!surface_pointer_valid(surface) ||
        surface->state == AURORA_GRAPHICS_SURFACE_FREE ||
        surface->capability_refs == UINT32_MAX) {
        spinlock_unlock(&surface_lock);
        return false;
    }

    ++surface->capability_refs;
    spinlock_unlock(&surface_lock);
    return true;
}

static void graphics_surface_cap_release(void *object) {
    struct aurora_graphics_surface *surface =
        (struct aurora_graphics_surface *)object;

    spinlock_lock(&surface_lock);

    if (surface_pointer_valid(surface) &&
        surface->state != AURORA_GRAPHICS_SURFACE_FREE &&
        surface->capability_refs != 0u) {
        --surface->capability_refs;
    }

    spinlock_unlock(&surface_lock);
    finalize_if_unreferenced(surface);
}

bool graphics_surface_system_init(void) {
    spinlock_init(&surface_lock);

    if (!cap_lifecycle_register(
            AURORA_CAP_SURFACE,
            graphics_surface_cap_retain,
            graphics_surface_cap_release)) {
        return false;
    }

    for (uint32_t i = 0u; i < AURORA_GRAPHICS_SURFACE_MAX_OBJECTS; ++i) {
        clear_surface(&surfaces[i]);
        surfaces[i].state = AURORA_GRAPHICS_SURFACE_FREE;
        surfaces[i].generation = 1u;
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

    uint32_t generation =
        slot->generation == 0u ? 1u : slot->generation;

    clear_surface(slot);
    slot->object_id = next_surface_id++;
    if (next_surface_id == 0u) next_surface_id = 1u;
    slot->generation = generation;
    slot->owner_refs = 1u;
    slot->capability_refs = 0u;
    slot->destroy_requested = false;
    slot->state = AURORA_GRAPHICS_SURFACE_READY;

    spinlock_unlock(&surface_lock);
    return slot;
}

bool graphics_surface_release_owner(
    struct aurora_graphics_surface *surface,
    uint32_t expected_generation
) {
    if (surface == NULL) return false;

    spinlock_lock(&surface_lock);

    if (!surface_pointer_valid(surface) ||
        surface->state == AURORA_GRAPHICS_SURFACE_FREE ||
        surface->generation != expected_generation ||
        surface->owner_refs == 0u) {
        spinlock_unlock(&surface_lock);
        return false;
    }

    --surface->owner_refs;
    surface->destroy_requested = true;

    spinlock_unlock(&surface_lock);
    finalize_if_unreferenced(surface);
    return true;
}

aurora_cap_handle graphics_surface_grant(
    struct aurora_cap_table *table,
    struct aurora_graphics_surface *surface,
    uint64_t rights
) {
    if (table == NULL ||
        surface == NULL ||
        surface->state == AURORA_GRAPHICS_SURFACE_FREE ||
        surface->destroy_requested ||
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
    struct aurora_capability_view buffer_view = {0};

    if (!graphics_surface_lookup(
            table,
            surface_handle,
            AURORA_RIGHT_WRITE,
            &surface) ||
        !graphics_buffer_lookup_retain(
            table,
            buffer_handle,
            AURORA_RIGHT_READ,
            &buffer,
            &buffer_view)) {
        return false;
    }

    if (!graphics_buffer_retain_surface(buffer)) {
        cap_view_release(&buffer_view);
        return false;
    }

    cap_view_release(&buffer_view);

    struct aurora_graphics_buffer *old_pending = NULL;

    spinlock_lock(&surface_lock);

    if (surface->state == AURORA_GRAPHICS_SURFACE_FREE ||
        buffer->state == AURORA_GRAPHICS_BUFFER_FREE ||
        buffer->memory == NULL) {
        spinlock_unlock(&surface_lock);
        graphics_buffer_release_surface(buffer);
        return false;
    }

    old_pending = surface->pending.buffer;
    surface->pending.buffer = buffer;
    surface->pending.damage_count = 0u;
    surface->pending.commit_serial = 0u;

    spinlock_unlock(&surface_lock);

    if (old_pending != NULL) {
        graphics_buffer_release_surface(old_pending);
    }

    return true;
}

bool graphics_surface_detach_buffers(
    struct aurora_cap_table *table,
    aurora_cap_handle surface_handle
) {
    struct aurora_graphics_surface *surface = NULL;

    if (!graphics_surface_lookup(
            table,
            surface_handle,
            AURORA_RIGHT_WRITE,
            &surface)) {
        return false;
    }

    struct aurora_graphics_buffer *pending = NULL;
    struct aurora_graphics_buffer *committed = NULL;

    spinlock_lock(&surface_lock);

    pending = surface->pending.buffer;
    committed = surface->committed.buffer;

    surface->pending.buffer = NULL;
    surface->pending.damage_count = 0u;
    surface->pending.commit_serial = 0u;

    surface->committed.buffer = NULL;
    surface->committed.damage_count = 0u;
    surface->committed.commit_serial = 0u;

    if (surface->state != AURORA_GRAPHICS_SURFACE_FREE) {
        surface->state = AURORA_GRAPHICS_SURFACE_READY;
    }

    spinlock_unlock(&surface_lock);

    if (pending != NULL) {
        graphics_buffer_release_surface(pending);
    }

    if (committed != NULL) {
        graphics_buffer_release_surface(committed);
    }

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

bool graphics_surface_request_frame_callback(
    struct aurora_cap_table *table,
    aurora_cap_handle surface_handle,
    uint64_t request_id
) {
    if (request_id == 0u) return false;

    struct aurora_graphics_surface *surface = NULL;

    if (!graphics_surface_lookup(
            table,
            surface_handle,
            AURORA_RIGHT_WRITE,
            &surface)) {
        return false;
    }

    spinlock_lock(&surface_lock);

    if (surface->state == AURORA_GRAPHICS_SURFACE_FREE ||
        surface->pending_frame_callback) {
        spinlock_unlock(&surface_lock);
        return false;
    }

    uint32_t free_slots = 0u;
    for (uint32_t i = 0u;
         i < AURORA_GRAPHICS_SURFACE_MAX_FRAME_CALLBACKS;
         ++i) {
        if (surface->frame_callbacks[i].state ==
            AURORA_GRAPHICS_FRAME_CALLBACK_FREE) {
            ++free_slots;
        }
    }

    if (free_slots == 0u) {
        spinlock_unlock(&surface_lock);
        return false;
    }

    surface->pending_frame_callback = true;
    surface->pending_frame_request_id = request_id;

    spinlock_unlock(&surface_lock);
    return true;
}

bool graphics_surface_complete_frame(
    struct aurora_graphics_surface *surface,
    uint64_t commit_serial,
    uint64_t presentation_serial
) {
    if (surface == NULL ||
        commit_serial == 0u ||
        presentation_serial == 0u) {
        return false;
    }

    spinlock_lock(&surface_lock);

    if (surface < &surfaces[0] ||
        surface >= &surfaces[AURORA_GRAPHICS_SURFACE_MAX_OBJECTS] ||
        surface->state == AURORA_GRAPHICS_SURFACE_FREE) {
        spinlock_unlock(&surface_lock);
        return false;
    }

    for (uint32_t i = 0u;
         i < AURORA_GRAPHICS_SURFACE_MAX_FRAME_CALLBACKS;
         ++i) {
        struct aurora_graphics_frame_callback *callback =
            &surface->frame_callbacks[i];

        if (callback->state ==
                AURORA_GRAPHICS_FRAME_CALLBACK_WAITING &&
            callback->commit_serial == commit_serial) {
            callback->presentation_serial = presentation_serial;
            callback->state =
                AURORA_GRAPHICS_FRAME_CALLBACK_READY;
            spinlock_unlock(&surface_lock);
            return true;
        }
    }

    spinlock_unlock(&surface_lock);
    return false;
}

bool graphics_surface_take_frame_callback(
    struct aurora_cap_table *table,
    aurora_cap_handle surface_handle,
    struct aurora_graphics_frame_callback *out_callback
) {
    if (out_callback == NULL) return false;

    struct aurora_graphics_surface *surface = NULL;

    if (!graphics_surface_lookup(
            table,
            surface_handle,
            AURORA_RIGHT_READ,
            &surface)) {
        return false;
    }

    spinlock_lock(&surface_lock);

    uint32_t selected =
        AURORA_GRAPHICS_SURFACE_MAX_FRAME_CALLBACKS;
    uint64_t oldest_commit = UINT64_MAX;

    for (uint32_t i = 0u;
         i < AURORA_GRAPHICS_SURFACE_MAX_FRAME_CALLBACKS;
         ++i) {
        struct aurora_graphics_frame_callback *callback =
            &surface->frame_callbacks[i];

        if (callback->state ==
                AURORA_GRAPHICS_FRAME_CALLBACK_READY &&
            callback->commit_serial < oldest_commit) {
            oldest_commit = callback->commit_serial;
            selected = i;
        }
    }

    if (selected ==
        AURORA_GRAPHICS_SURFACE_MAX_FRAME_CALLBACKS) {
        spinlock_unlock(&surface_lock);
        return false;
    }

    *out_callback = surface->frame_callbacks[selected];

    surface->frame_callbacks[selected].request_id = 0u;
    surface->frame_callbacks[selected].commit_serial = 0u;
    surface->frame_callbacks[selected].presentation_serial = 0u;
    surface->frame_callbacks[selected].state =
        AURORA_GRAPHICS_FRAME_CALLBACK_FREE;

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

    struct aurora_graphics_frame_callback *callback_slot = NULL;

    if (surface->pending_frame_callback) {
        for (uint32_t i = 0u;
             i < AURORA_GRAPHICS_SURFACE_MAX_FRAME_CALLBACKS;
             ++i) {
            if (surface->frame_callbacks[i].state ==
                AURORA_GRAPHICS_FRAME_CALLBACK_FREE) {
                callback_slot = &surface->frame_callbacks[i];
                break;
            }
        }

        if (callback_slot == NULL) {
            spinlock_unlock(&surface_lock);
            return false;
        }
    }

    if (!graphics_buffer_retain_surface(buffer)) {
        spinlock_unlock(&surface_lock);
        return false;
    }

    if (callback_slot != NULL) {
        callback_slot->request_id =
            surface->pending_frame_request_id;
        callback_slot->commit_serial = serial;
        callback_slot->presentation_serial = 0u;
        callback_slot->state =
            AURORA_GRAPHICS_FRAME_CALLBACK_WAITING;

        surface->pending_frame_callback = false;
        surface->pending_frame_request_id = 0u;
    }

    struct aurora_graphics_buffer *old_committed =
        surface->committed.buffer;

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

    if (old_committed != NULL) {
        graphics_buffer_release_surface(old_committed);
    }

    if (out_commit_serial != NULL) {
        *out_commit_serial = serial;
    }

    return true;
}

bool graphics_surface_read_committed(
    struct aurora_cap_table *table,
    aurora_cap_handle surface_handle,
    struct aurora_graphics_surface_snapshot *out_snapshot
) {
    if (out_snapshot == NULL) return false;

    *out_snapshot = (struct aurora_graphics_surface_snapshot){0};

    struct aurora_graphics_surface *surface = NULL;

    if (!graphics_surface_lookup(
            table,
            surface_handle,
            AURORA_RIGHT_READ,
            &surface)) {
        return false;
    }

    spinlock_lock(&surface_lock);

    if (surface->state == AURORA_GRAPHICS_SURFACE_FREE ||
        surface->committed.buffer == NULL ||
        surface->committed.commit_serial == 0u ||
        !graphics_buffer_retain_surface(
            surface->committed.buffer)) {
        spinlock_unlock(&surface_lock);
        return false;
    }

    *out_snapshot = surface->committed;

    spinlock_unlock(&surface_lock);
    return true;
}

void graphics_surface_snapshot_release(
    struct aurora_graphics_surface_snapshot *snapshot
) {
    if (snapshot == NULL) return;

    struct aurora_graphics_buffer *buffer =
        snapshot->buffer;

    *snapshot =
        (struct aurora_graphics_surface_snapshot){0};

    if (buffer != NULL) {
        graphics_buffer_release_surface(buffer);
    }
}
