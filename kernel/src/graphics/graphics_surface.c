#include <stddef.h>
#include <stdint.h>

#include <aurora/graphics_surface.h>

static struct aurora_graphics_surface surfaces[AURORA_GRAPHICS_SURFACE_MAX_OBJECTS];
static aurora_spinlock surface_lock = AURORA_SPINLOCK_INIT;
static uint64_t next_surface_id;
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
