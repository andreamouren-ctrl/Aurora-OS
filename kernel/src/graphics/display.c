#include <stddef.h>
#include <stdint.h>

#include <aurora/display.h>
#include <aurora/display_boot_framebuffer.h>

static struct aurora_boot_framebuffer_backend boot_backend;
static const struct aurora_display_output *outputs[AURORA_DISPLAY_MAX_OUTPUTS];
static uint32_t output_count;

static void clear_registry(void) {
    for (uint32_t i = 0u; i < AURORA_DISPLAY_MAX_OUTPUTS; ++i) {
        outputs[i] = NULL;
    }

    output_count = 0u;
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
