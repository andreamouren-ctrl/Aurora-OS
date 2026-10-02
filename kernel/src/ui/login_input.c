#include <stddef.h>

#include <aurora/bootstrap_probe.h>
#include <aurora/input.h>
#include <aurora/login_input.h>
#include <aurora/login_ui.h>
#include <aurora/nvme.h>

#define AURORA_KEY_MIN_LENGTH 12u
#define AURORA_KEY_MAX_LENGTH 32u

static char credential_buffer[AURORA_KEY_MAX_LENGTH + 1u];
static size_t credential_length;

static void clear_credential(void) {
    for (size_t i = 0u; i < sizeof(credential_buffer); ++i) {
        credential_buffer[i] = '\0';
    }

    credential_length = 0u;
    login_ui_set_masked_length(0u);
}

static char key_to_normalized_character(
    enum aurora_key_code key
) {
    if (key >= AURORA_KEY_A && key <= AURORA_KEY_Z) {
        return (char)('A' + (key - AURORA_KEY_A));
    }

    if (key >= AURORA_KEY_0 && key <= AURORA_KEY_9) {
        return (char)('0' + (key - AURORA_KEY_0));
    }

    return '\0';
}

static void handle_pressed_key(
    enum aurora_key_code key
) {
    if (key == AURORA_KEY_ESCAPE) {
        clear_credential();
        login_ui_set_state(AURORA_LOGIN_IDLE);
        return;
    }

    if (key == AURORA_KEY_BACKSPACE) {
        if (credential_length != 0u) {
            --credential_length;
            credential_buffer[credential_length] = '\0';
            login_ui_set_masked_length(credential_length);
        }

        login_ui_set_state(AURORA_LOGIN_IDLE);
        return;
    }

    if (key == AURORA_KEY_ENTER) {
        if (credential_length < AURORA_KEY_MIN_LENGTH) {
            login_ui_set_state(AURORA_LOGIN_ERROR);
            return;
        }

        login_ui_set_state(AURORA_LOGIN_AUTHENTICATING);

        /*
         * The persistent Aurora Identity Service does not exist yet. Do not
         * fabricate an account lookup or verifier result in kernel space.
         * Wipe the submitted secret and return to the existing safe error
         * state until the real user-space verifier is introduced.
         */
        clear_credential();
        login_ui_set_state(AURORA_LOGIN_ERROR);
        return;
    }

    char normalized = key_to_normalized_character(key);

    if (normalized == '\0' ||
        credential_length >= AURORA_KEY_MAX_LENGTH) {
        return;
    }

    credential_buffer[credential_length++] = normalized;
    credential_buffer[credential_length] = '\0';

    login_ui_set_masked_length(credential_length);
    login_ui_set_state(AURORA_LOGIN_IDLE);
}

void login_input_init(void) {
    /*
     * Temporary M1 bootstrap hook: execute storage contracts before the login
     * surface starts accepting credentials. This probe will move out of the
     * UI path once Aurora has a dedicated service/bootstrap manager.
     */
    bootstrap_storage_probe();
    nvme_bootstrap_probe();

    struct aurora_block_device *nvme = nvme_namespace_block_device();
    if (nvme != NULL) {
        bootstrap_storage_probe_device(nvme, "NVMe");
    }

    credential_length = 0u;

    for (size_t i = 0u; i < sizeof(credential_buffer); ++i) {
        credential_buffer[i] = '\0';
    }

    login_ui_set_masked_length(0u);
    login_ui_set_state(AURORA_LOGIN_IDLE);
}

void login_input_pump(void) {
    struct aurora_input_event event;

    while (input_poll_event(&event)) {
        if (!event.pressed) {
            continue;
        }

        handle_pressed_key(event.key);
    }
}
