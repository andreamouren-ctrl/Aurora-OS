#include <stddef.h>

#include <aurora/bootstrap_probe.h>
#include <aurora/entropy.h>
#include <aurora/entropy_ring3_probe.h>
#include <aurora/identity_auth_probe.h>
#include <aurora/identity_client.h>
#include <aurora/identity_create_probe.h>
#include <aurora/input.h>
#include <aurora/ipc_wait_probe.h>
#include <aurora/log.h>
#include <aurora/login_input.h>
#include <aurora/login_ui.h>
#include <aurora/nvme.h>
#include <aurora/panic.h>
#include <aurora/protected_state.h>
#include <aurora/protected_state_user_probe.h>
#include <aurora/service_bootstrap.h>
#include <aurora/service_supervisor.h>
#include <aurora/vfs.h>

#define AURORA_KEY_MIN_LENGTH 12u
#define AURORA_KEY_MAX_LENGTH 32u

static char credential_buffer[AURORA_KEY_MAX_LENGTH + 1u];
static size_t credential_length;
static bool create_offer_active;
static bool create_entry_mode;
static bool creation_notice_active;

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

static void synchronize_identity_state(void) {
    identity_client_pump();

    switch (identity_client_state()) {
        case AURORA_IDENTITY_CLIENT_AUTH_FAILED:
            clear_credential();
            create_offer_active = true;
            create_entry_mode = false;
            creation_notice_active = false;
            login_ui_set_state(AURORA_LOGIN_UNKNOWN_IDENTITY);
            identity_client_reset_result();
            return;

        case AURORA_IDENTITY_CLIENT_THROTTLED:
            clear_credential();
            create_offer_active = false;
            create_entry_mode = false;
            creation_notice_active = false;
            login_ui_set_state(AURORA_LOGIN_THROTTLED);
            identity_client_reset_result();
            return;

        case AURORA_IDENTITY_CLIENT_VERIFIED:
            clear_credential();
            create_offer_active = false;
            create_entry_mode = false;
            creation_notice_active = false;
            /*
             * Identity verification succeeded, but Aurora does not yet have the
             * Session Manager that consumes the opaque grant and establishes a
             * desktop session. Keep the surface in its authenticating state
             * rather than pretending that kernel UI code can create a session.
             */
            login_ui_set_state(AURORA_LOGIN_AUTHENTICATING);
            return;

        case AURORA_IDENTITY_CLIENT_CREATING:
            login_ui_set_state(AURORA_LOGIN_CREATING);
            return;

        case AURORA_IDENTITY_CLIENT_CREATED:
            clear_credential();
            create_offer_active = false;
            create_entry_mode = false;
            creation_notice_active = true;
            login_ui_set_state(AURORA_LOGIN_CREATED);
            identity_client_reset_result();
            return;

        case AURORA_IDENTITY_CLIENT_CREATE_EXISTS:
        case AURORA_IDENTITY_CLIENT_CREATE_DENIED:
            clear_credential();
            create_offer_active = false;
            create_entry_mode = false;
            creation_notice_active = true;
            login_ui_set_state(AURORA_LOGIN_CREATE_DENIED);
            identity_client_reset_result();
            return;

        case AURORA_IDENTITY_CLIENT_UNAVAILABLE:
        case AURORA_IDENTITY_CLIENT_ERROR:
            clear_credential();
            create_offer_active = false;
            create_entry_mode = false;
            creation_notice_active = false;
            login_ui_set_state(AURORA_LOGIN_ERROR);
            return;

        case AURORA_IDENTITY_CLIENT_AUTHENTICATING:
            login_ui_set_state(AURORA_LOGIN_AUTHENTICATING);
            return;

        case AURORA_IDENTITY_CLIENT_READY:
        case AURORA_IDENTITY_CLIENT_UNINITIALIZED:
        default:
            return;
    }
}

static void handle_pressed_key(
    enum aurora_key_code key
) {
    enum aurora_identity_client_state state = identity_client_state();
    if (state == AURORA_IDENTITY_CLIENT_AUTHENTICATING ||
        state == AURORA_IDENTITY_CLIENT_VERIFIED ||
        state == AURORA_IDENTITY_CLIENT_CREATING) {
        return;
    }

    if (create_offer_active) {
        if (key == AURORA_KEY_ESCAPE) {
            create_offer_active = false;
            clear_credential();
            identity_client_reset_result();
            login_ui_set_state(AURORA_LOGIN_IDLE);
        } else if (key == AURORA_KEY_ENTER) {
            create_offer_active = false;
            create_entry_mode = true;
            creation_notice_active = false;
            clear_credential();
            identity_client_reset_result();
            login_ui_set_state(AURORA_LOGIN_CREATE_ENTRY);
        }
        return;
    }

    if (creation_notice_active) {
        if (key == AURORA_KEY_ENTER || key == AURORA_KEY_ESCAPE) {
            creation_notice_active = false;
            create_entry_mode = false;
            clear_credential();
            identity_client_reset_result();
            login_ui_set_state(AURORA_LOGIN_IDLE);
        }
        return;
    }

    if (key == AURORA_KEY_ESCAPE) {
        clear_credential();
        create_offer_active = false;
        create_entry_mode = false;
        creation_notice_active = false;
        identity_client_reset_result();
        login_ui_set_state(AURORA_LOGIN_IDLE);
        return;
    }

    if (key == AURORA_KEY_BACKSPACE) {
        if (credential_length != 0u) {
            --credential_length;
            credential_buffer[credential_length] = '\0';
            login_ui_set_masked_length(credential_length);
        }

        identity_client_reset_result();
        login_ui_set_state(
            create_entry_mode
                ? AURORA_LOGIN_CREATE_ENTRY
                : AURORA_LOGIN_IDLE);
        return;
    }

    if (key == AURORA_KEY_ENTER) {
        if (credential_length < AURORA_KEY_MIN_LENGTH) {
            login_ui_set_state(AURORA_LOGIN_ERROR);
            return;
        }

        bool submitted = create_entry_mode
            ? identity_client_begin_create(
                credential_buffer,
                credential_length)
            : identity_client_begin_key_auth(
                credential_buffer,
                credential_length);

        if (!submitted) {
            clear_credential();
            create_entry_mode = false;
            login_ui_set_state(AURORA_LOGIN_ERROR);
            return;
        }

        clear_credential();
        login_ui_set_state(
            create_entry_mode
                ? AURORA_LOGIN_CREATING
                : AURORA_LOGIN_AUTHENTICATING);
        return;
    }

    char normalized = key_to_normalized_character(key);

    if (normalized == '\0' ||
        credential_length >= AURORA_KEY_MAX_LENGTH) {
        return;
    }

    identity_client_reset_result();
    credential_buffer[credential_length++] = normalized;
    credential_buffer[credential_length] = '\0';

    login_ui_set_masked_length(credential_length);
    login_ui_set_state(
        create_entry_mode
            ? AURORA_LOGIN_CREATE_ENTRY
            : AURORA_LOGIN_IDLE);
}

#if AURORA_BOOT_VALIDATION
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

    if (!identity_auth_ring3_self_test()) {
        kernel_panic("Ring 3 Identity key-auth protocol self-test failed");
    }

    log_line("[identity-auth] capability-authorized key-auth protocol + degraded fail-closed path passed");

    if (!identity_create_ring3_self_test()) {
        kernel_panic("Ring 3 Identity create protocol self-test failed");
    }

    log_line("[identity-create] capability-authorized create/cancel + degraded fail-closed path passed");

    if (!service_supervisor_self_test()) {
        kernel_panic("Trusted Ring 3 service supervisor restart self-test failed");
    }

    log_line("[service-supervisor] bounded Identity restart + fresh capability bootstrap passed");
}

#endif
void login_input_init(void) {
    /*
     * Production boot performs only initialization required by the live
     * system. Deep storage/service/IPC probes remain available in validation
     * builds used by CI.
     */
    bootstrap_storage_probe();
    nvme_bootstrap_probe();

    struct aurora_block_device *nvme = nvme_namespace_block_device();
    if (nvme != NULL) {
        bootstrap_storage_probe_device(nvme, "NVMe");
    }

#if AURORA_BOOT_VALIDATION
    if (!ipc_wait_ring3_self_test()) {
        kernel_panic("Ring 3 IPC blocking wait/wakeup self-test failed");
    }

    log_line("[ring3-ipc] blocking wait/wakeup syscall probe passed");

    if (entropy_ready()) {
        if (!entropy_ring3_self_test()) {
            kernel_panic("Ring 3 entropy seed syscall self-test failed");
        }
        log_line("[ring3-entropy] capability-gated trusted seed syscall probe passed");
    } else {
        log_line("[ring3-entropy] trusted seed unavailable; capability probe skipped");
    }

    protected_state_bootstrap_probe();
#endif

    credential_length = 0u;
    create_offer_active = false;
    create_entry_mode = false;
    creation_notice_active = false;

    for (size_t i = 0u; i < sizeof(credential_buffer); ++i) {
        credential_buffer[i] = '\0';
    }

    bool identity_ready = identity_client_init();

    /*
     * Decode and publish the final artwork exactly once, after the live
     * Identity service initialization. Until this point the boot splash stays
     * on screen, so the emergency fallback never flashes during a normal boot.
     */
    if (login_ui_activate_native_artwork()) {
        log_line("[identity-gui] full-quality PNG artwork active");
    } else {
        log_line("[identity-gui] native artwork unavailable; procedural fallback active");
        login_ui_render();
    }

    if (identity_ready) {
        log_line("[identity-client] production Ring 3 Identity service connected to login input");
        login_ui_set_state(AURORA_LOGIN_IDLE);
    } else {
        log_line("[identity-client] Identity service unavailable; login remains fail-closed");
        login_ui_set_state(AURORA_LOGIN_ERROR);
    }
}

void login_input_pump(void) {
    synchronize_identity_state();

    struct aurora_input_event event;

    while (input_poll_event(&event)) {
        if (!event.pressed) {
            continue;
        }

        handle_pressed_key(event.key);
    }
}
