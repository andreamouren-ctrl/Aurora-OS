#include <stddef.h>
#include <stdint.h>

#include <aurora/block_device.h>
#include <aurora/bootstrap_probe.h>
#include <aurora/input.h>
#include <aurora/kernel.h>
#include <aurora/login_input.h>
#include <aurora/login_ui.h>
#include <aurora/log.h>
#include <aurora/nvme.h>
#include <aurora/protected_state.h>
#include <aurora/protected_state_user_probe.h>
#include <aurora/service_bootstrap.h>
#include <aurora/service_supervisor.h>
#include <aurora/vfs.h>

#define AURORA_KEY_MIN_LENGTH 12u
#define AURORA_KEY_MAX_LENGTH 32u

static char credential[AURORA_KEY_MAX_LENGTH + 1u];
static size_t credential_length;

static void clear_credential(void) {
    for (size_t i = 0; i < sizeof(credential); ++i) credential[i] = '\0';
    credential_length = 0;
    login_ui_set_masked_length(0);
}

static bool normalize_symbol(char input, char *out) {
    if (out == NULL) return false;

    if (input >= 'a' && input <= 'z') {
        *out = (char)(input - ('a' - 'A'));
        return true;
    }

    if ((input >= 'A' && input <= 'Z') ||
        (input >= '0' && input <= '9')) {
        *out = input;
        return true;
    }

    return false;
}

static void submit_credential(void) {
    if (credential_length < AURORA_KEY_MIN_LENGTH) {
        login_ui_set_state(AURORA_LOGIN_INVALID);
        return;
    }

    login_ui_set_state(AURORA_LOGIN_CHECKING);

    /*
     * M1 bootstrap only. Real authentication moves into the isolated Aurora
     * Identity Service; the framebuffer login remains a recovery/bootstrap UI.
     */
    login_ui_set_state(AURORA_LOGIN_ACCEPTED);
}

static void handle_key_event(const struct aurora_input_event *event) {
    if (event == NULL || event->type != AURORA_INPUT_KEY_DOWN) return;

    if (event->key == AURORA_KEY_BACKSPACE) {
        if (credential_length != 0u) {
            --credential_length;
            credential[credential_length] = '\0';
        }
        login_ui_set_masked_length(credential_length);
        login_ui_set_state(AURORA_LOGIN_IDLE);
        return;
    }

    if (event->key == AURORA_KEY_ENTER) {
        submit_credential();
        return;
    }

    char normalized = '\0';
    if (!normalize_symbol(event->character, &normalized)) return;
    if (credential_length >= AURORA_KEY_MAX_LENGTH) return;

    credential[credential_length++] = normalized;
    credential[credential_length] = '\0';
    login_ui_set_masked_length(credential_length);
    login_ui_set_state(AURORA_LOGIN_IDLE);
}

static void protected_state_bootstrap_probe(void) {
    struct aurora_vfs_stat system_stat;

    if (!vfs_stat("/system", &system_stat) ||
        system_stat.type != AURORA_VFS_NODE_DIRECTORY) {
        log_line("[protected-state] /system unavailable; runtime self-test skipped");
        return;
    }

    if (!protected_state_self_test()) {
        kernel_panic("Protected system-state capability self-test failed");
    }

    log_line("[protected-state] capability-gated /system state self-test passed");

    if (!protected_state_ring3_self_test()) {
        kernel_panic("Ring 3 Protected State syscall self-test failed");
    }

    log_line("[ring3-protected-state] capability-gated record syscall probe passed");

    if (!service_bootstrap_self_test()) {
        kernel_panic("Trusted Ring 3 Identity service bootstrap self-test failed");
    }

    log_line("[service] trusted Ring 3 Identity bootstrap + least-privilege capability assignment passed");
    log_line("[identity-service] long-lived blocking IPC request loop passed");

    if (!service_supervisor_self_test()) {
        kernel_panic("Trusted Ring 3 service supervisor restart self-test failed");
    }

    log_line("[service-supervisor] bounded Identity restart + fresh capability bootstrap passed");
}

void login_input_init(void) {
    /*
     * Temporary M1 bootstrap hook: execute storage and trusted-service contracts
     * before the login surface starts accepting credentials. This policy moves
     * out of the UI path when Aurora gains the production Service Manager.
     */
    bootstrap_storage_probe();
    nvme_bootstrap_probe();

    struct aurora_block_device *nvme = nvme_namespace_block_device();
    if (nvme != NULL) {
        bootstrap_storage_probe_device(nvme, "NVMe");
    }

    protected_state_bootstrap_probe();
    input_init();
    login_ui_init();
    clear_credential();
}

void login_input_pump(void) {
    struct aurora_input_event event;
    while (input_pop_event(&event)) handle_key_event(&event);
}
