#include <stddef.h>
#include <stdint.h>

#include <aurora/display.h>
#include <aurora/display_boot_framebuffer.h>

static struct aurora_boot_framebuffer_backend boot_backend;
static const struct aurora_display_output *outputs[AURORA_DISPLAY_MAX_OUTPUTS];
static uint32_t output_count;
static struct aurora_display_present_state present_states[AURORA_DISPLAY_MAX_OUTPUTS];
static struct aurora_gpu_display_device native_gpu_device;
static bool native_gpu_attached;

static void clear_registry(void) {
    for (uint32_t i = 0u; i < AURORA_DISPLAY_MAX_OUTPUTS; ++i) {
        outputs[i] = NULL;
        present_states[i].next_serial = 1u;
        present_states[i].last_presented_serial = 0u;
        present_states[i].last_released_serial = 0u;
    }

    output_count = 0u;
    native_gpu_device = (struct aurora_gpu_display_device){0};
    native_gpu_attached = false;
}

static bool register_output(
    const struct aurora_display_output *output
) {
    if (output == NULL ||
        !output->connected ||
        output->id == 0u ||
        output_count >= AURORA_DISPLAY_MAX_OUTPUTS) {
        return false;
    }

    for (uint32_t i = 0u; i < output_count; ++i) {
        if (outputs[i] != NULL &&
            outputs[i]->id == output->id) {
            return false;
        }
    }

    outputs[output_count++] = output;
    return true;
}

bool display_init_bootstrap(
    const struct aurora_framebuffer *framebuffer
) {
    clear_registry();

    if (!display_boot_framebuffer_init(
            &boot_backend,
            framebuffer)) {
        return false;
    }

    const struct aurora_display_output *output =
        display_boot_framebuffer_output(&boot_backend);

    if (!register_output(output)) {
        clear_registry();
        return false;
    }

    return true;
}

uint32_t display_output_count(void) {
    return output_count;
}

const struct aurora_display_output *display_output_at(
    uint32_t output_index
) {
    if (output_index >= output_count) {
        return NULL;
    }

    return outputs[output_index];
}

const struct aurora_display_mode *display_mode_at(
    uint32_t output_index,
    uint32_t mode_index
) {
    const struct aurora_display_output *output =
        display_output_at(output_index);

    if (output == NULL ||
        mode_index >= output->mode_count) {
        return NULL;
    }

    return &output->modes[mode_index];
}

bool display_present(
    uint32_t output_index,
    struct aurora_display_backbuffer *buffer,
    uint64_t *out_present_serial
) {
    const struct aurora_display_output *output =
        display_output_at(output_index);

    if (output == NULL ||
        buffer == NULL ||
        !buffer->ready ||
        buffer->pixels == NULL ||
        buffer->in_flight ||
        output_index >= AURORA_DISPLAY_MAX_OUTPUTS) {
        return false;
    }

    struct aurora_display_present_state *state =
        &present_states[output_index];

    uint64_t serial = state->next_serial;

    if (serial == 0u) {
        serial = 1u;
    }

    buffer->in_flight = true;

    bool presented = false;

    switch (output->backend) {
        case AURORA_DISPLAY_BACKEND_BOOT_FRAMEBUFFER:
            if (output == &boot_backend.output) {
                presented = display_boot_framebuffer_present(
                    &boot_backend,
                    buffer
                );
            }
            break;

        default:
            break;
    }

    if (!presented) {
        buffer->in_flight = false;
        return false;
    }

    state->last_presented_serial = serial;

    /*
     * The boot framebuffer backend is synchronous: once the bounded copy
     * returns, it no longer reads the source buffer. Therefore release is
     * signaled immediately. Future asynchronous/GPU backends may defer this
     * transition until their completion interrupt/fence fires.
     */
    state->last_released_serial = serial;
    buffer->in_flight = false;

    if (buffer->generation == UINT64_MAX) {
        buffer->generation = 1u;
    } else {
        ++buffer->generation;
    }

    state->next_serial =
        serial == UINT64_MAX ? 1u : serial + 1u;

    if (out_present_serial != NULL) {
        *out_present_serial = serial;
    }

    return true;
}

bool display_present_state(
    uint32_t output_index,
    struct aurora_display_present_state *out_state
) {
    if (out_state == NULL ||
        output_index >= output_count ||
        output_index >= AURORA_DISPLAY_MAX_OUTPUTS) {
        return false;
    }

    *out_state = present_states[output_index];
    return true;
}

bool display_attach_native_gpu(
    const struct aurora_gpu_display_device *device
) {
    if (device == NULL ||
        !device->bound ||
        device->driver_name == NULL ||
        device->controller.ops == NULL ||
        native_gpu_attached) {
        return false;
    }

    native_gpu_device = *device;
    native_gpu_attached = true;
    return true;
}

bool display_native_gpu_ready(void) {
    return native_gpu_attached;
}

const struct aurora_gpu_display_device *display_native_gpu_device(void) {
    return native_gpu_attached ? &native_gpu_device : NULL;
}
