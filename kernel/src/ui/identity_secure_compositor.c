#include <stddef.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/display.h>
#include <aurora/graphics_buffer.h>
#include <aurora/graphics_surface.h>
#include <aurora/heap.h>
#include <aurora/identity_secure_compositor.h>
#include <aurora/identity_ring3_host.h>
#include <aurora/log.h>
#include <aurora/pmm.h>
#include <aurora/software_compositor.h>

/* The boot framebuffer backend is synchronous. This deliberately bounded V1
 * bridge repaints a single owned buffer only after the preceding present has
 * returned. Do not reuse this policy with asynchronous GPU scanout. */
#define IDENTITY_SECURE_MAX_STAGING_BYTES (16u * 1024u * 1024u)

struct identity_secure_host {
    struct aurora_cap_table caps;
    struct aurora_software_compositor compositor;
    struct aurora_graphics_buffer *buffer;
    struct aurora_graphics_surface *surface;
    struct aurora_framebuffer staging_fb;
    uint8_t *staging_pixels;
    size_t staging_bytes;
    aurora_cap_handle buffer_cap;
    aurora_cap_handle surface_cap;
    aurora_cap_handle display_control;
    uint64_t node;
    uint64_t last_present;
    bool caps_ready;
    bool active;
    bool ring3_present_logged;
};

static struct identity_secure_host host;
static bool disabled_until_reboot;

static void clear_host(void) {
    uint8_t *bytes = (uint8_t *)&host;
    for (size_t i = 0u; i < sizeof(host); ++i) bytes[i] = 0u;
}

void identity_secure_compositor_end(void) {
    /* Revoke Ring3 node/handles first while the compositor and process are
     * both valid. The kernel-owned login stays fully independently usable. */
    identity_ring3_host_detach_badge();
    if (host.compositor.initialized) {
        if (host.node != 0u)
            (void)software_compositor_remove_surface(&host.compositor, host.node);
        (void)software_compositor_destroy(&host.compositor);
    }
    if (host.caps_ready) {
        if (host.surface_cap != AURORA_CAP_INVALID)
            (void)graphics_surface_detach_buffers(&host.caps, host.surface_cap);
        cap_table_destroy(&host.caps);
    }
    if (host.surface != NULL)
        (void)graphics_surface_release_owner(host.surface, host.surface->generation);
    if (host.buffer != NULL)
        (void)graphics_buffer_release_owner(host.buffer, host.buffer->generation);
    if (host.staging_pixels != NULL && host.staging_bytes != 0u)
        (void)kheap_free_sized(host.staging_pixels, host.staging_bytes);
    clear_host();
}

static bool native_mode_matches(const struct aurora_framebuffer *native,
                                const struct aurora_display_mode *mode,
                                const struct aurora_display_output *output) {
    return native != NULL && mode != NULL && output != NULL &&
           output->backend == AURORA_DISPLAY_BACKEND_BOOT_FRAMEBUFFER &&
           native->address != NULL && native->bpp == 32u &&
           native->width == mode->width && native->height == mode->height &&
           mode->format.encoding == AURORA_PIXEL_ENCODING_UNORM_PACKED &&
           mode->format.bits_per_pixel == 32u &&
           mode->format.alpha_mask_size == 0u &&
           native->red_mask_size == mode->format.red_mask_size &&
           native->red_mask_shift == mode->format.red_mask_shift &&
           native->green_mask_size == mode->format.green_mask_size &&
           native->green_mask_shift == mode->format.green_mask_shift &&
           native->blue_mask_size == mode->format.blue_mask_size &&
           native->blue_mask_shift == mode->format.blue_mask_shift &&
           native->width != 0u && native->height != 0u &&
           native->width <= IDENTITY_SECURE_MAX_STAGING_BYTES / 4u &&
           native->height <=
               IDENTITY_SECURE_MAX_STAGING_BYTES / (native->width * 4u);
}

bool identity_secure_compositor_begin(const struct aurora_framebuffer *native) {
    if (host.active) return true;
    if (disabled_until_reboot) return false;

    const struct aurora_display_mode *mode = display_mode_at(0u, 0u);
    const struct aurora_display_output *output = display_output_at(0u);
    if (!native_mode_matches(native, mode, output)) {
        disabled_until_reboot = true;
        return false;
    }

    clear_host();
    host.staging_bytes = (size_t)(native->width * native->height * 4u);
    host.staging_pixels = (uint8_t *)kheap_alloc(host.staging_bytes, 16u);
    if (host.staging_pixels == NULL) goto failure;

    cap_table_init(&host.caps);
    host.caps_ready = true;
    host.buffer = graphics_buffer_create(native->width, native->height,
                                         &mode->format);
    host.surface = graphics_surface_create();
    if (host.buffer == NULL || host.surface == NULL) goto failure;

    /* Exactly three capabilities: read-only pixel buffer, writable owned
     * surface, display-control privilege. No user/profile/Identity secrets. */
    host.buffer_cap = graphics_buffer_grant(
        &host.caps, host.buffer, AURORA_RIGHT_READ);
    host.surface_cap = graphics_surface_grant(
        &host.caps, host.surface, AURORA_RIGHT_READ | AURORA_RIGHT_WRITE);
    host.display_control = cap_grant(
        &host.caps, (void *)output, AURORA_CAP_DISPLAY, AURORA_RIGHT_CONTROL);
    if (host.buffer_cap == AURORA_CAP_INVALID ||
        host.surface_cap == AURORA_CAP_INVALID ||
        host.display_control == AURORA_CAP_INVALID ||
        !graphics_surface_attach(&host.caps, host.surface_cap, host.buffer_cap) ||
        !software_compositor_init(&host.compositor, 0u) ||
        !software_compositor_add_privileged_surface(
            &host.compositor, &host.caps, host.display_control, host.surface,
            AURORA_COMPOSITOR_SURFACE_PRE_SESSION,
            0, 0, 100, 255u, &host.node) ||
        !software_compositor_set_secure_scene(
            &host.compositor, &host.caps, host.display_control, true) ||
        !software_compositor_node_in_active_scene(&host.compositor, host.node))
        goto failure;

    host.staging_fb = *native;
    host.staging_fb.address = host.staging_pixels;
    host.staging_fb.pitch = native->width * 4u;
    host.active = true;
    /* Optional bounded Ring3-owned visual accent; if the isolated worker is
     * unavailable the complete kernel-rendered login remains unchanged. */
    (void)identity_ring3_host_attach_badge(
        &host.compositor,&host.caps,host.display_control,
        (uint32_t)native->width,(uint32_t)native->height);
    return true;

failure:
    identity_secure_compositor_end();
    disabled_until_reboot = true;
    return false;
}

const struct aurora_framebuffer *identity_secure_compositor_target(void) {
    return host.active ? &host.staging_fb : NULL;
}

bool identity_secure_compositor_active(void) {
    return host.active && host.last_present != 0u;
}

bool identity_secure_compositor_present(void) {
    if (!host.active || host.staging_pixels == NULL ||
        host.buffer == NULL || host.buffer->memory == NULL ||
        !host.compositor.secure_scene_active ||
        !software_compositor_node_in_active_scene(&host.compositor, host.node))
        goto failure;

    /* Buffer layout is tightly packed; a memory object may consist of
     * discontiguous physical pages. Never treat the pages as one array. */
    size_t consumed = 0u;
    for (uint32_t page = 0u; page < host.buffer->memory->page_count &&
                               consumed < host.staging_bytes; ++page) {
        uint64_t physical = 0u;
        if (!memory_object_page_at(host.buffer->memory, page, &physical))
            goto failure;
        uint8_t *dest = (uint8_t *)pmm_phys_to_virt(physical);
        if (dest == NULL) goto failure;
        size_t length = host.staging_bytes - consumed;
        if (length > AURORA_PAGE_SIZE) length = AURORA_PAGE_SIZE;
        for (size_t byte = 0u; byte < length; ++byte)
            dest[byte] = host.staging_pixels[consumed + byte];
        consumed += length;
    }
    if (consumed != host.staging_bytes) goto failure;

    const struct aurora_graphics_rect full = {
        .x = 0u, .y = 0u,
        .width = (uint32_t)host.staging_fb.width,
        .height = (uint32_t)host.staging_fb.height
    };
    uint64_t commit = 0u;
    uint64_t serial = 0u;
    if (!graphics_surface_damage(&host.caps, host.surface_cap, &full) ||
        !graphics_surface_commit(&host.caps, host.surface_cap, &commit) ||
        commit == 0u ||
        !software_compositor_compose_present(&host.compositor, &serial) ||
        serial == 0u) goto failure;

    host.last_present = serial;
    if (!host.ring3_present_logged && identity_ring3_host_badge_active()) {
        host.ring3_present_logged = true;
        log_line("[identity-g2-ring3] Ring3-owned pixels composed on trusted PRE_SESSION framebuffer");
    }
    return true;

failure:
    /* Rendering failure must never become authentication success. Disable
     * retries this boot, revoke scene resources, then let the caller paint
     * the independent direct-framebuffer recovery renderer. */
    disabled_until_reboot = true;
    identity_secure_compositor_end();
    return false;
}
