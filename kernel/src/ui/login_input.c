#include <stddef.h>

#include <aurora/bootstrap_probe.h>
#include <aurora/entropy.h>
#include <aurora/clock.h>
#include <aurora/display_ring3_probe.h>
#include <aurora/entropy_ring3_probe.h>
#include <aurora/graphics_ring3_probe.h>
#include <aurora/identity_auth_probe.h>
#include <aurora/identity_client.h>
#include <aurora/identity_create_probe.h>
#include <aurora/identity_session_grant_probe.h>
#include <aurora/identity_reauth_probe.h>
#include <aurora/identity_key_rotation_probe.h>
#include <aurora/input.h>
#include <aurora/ipc_wait_probe.h>
#include <aurora/g5_ipc_endpoint.h>
#include <aurora/g5_ipc_durable.h>
#include <aurora/aurora_fs_v2_namespace_txn.h>
#include <aurora/log.h>
#include <aurora/login_input.h>
#include <aurora/login_ui.h>
#include <aurora/nvme.h>
#include <aurora/panic.h>
#include <aurora/protected_state.h>
#include <aurora/protected_state_user_probe.h>
#include <aurora/service_bootstrap.h>
#include <aurora/service_supervisor.h>
#include <aurora/session_manager_client.h>
#include <aurora/session_manager_probe.h>
#include <aurora/security_activity_model_probe.h>
#include <aurora/security_activity_renderer_probe.h>
#if AURORA_BOOT_VALIDATION
#include <aurora/security_activity_graphics_probe.h>
#endif
#include <aurora/user_session_host.h>
#include <aurora/vfs.h>

#define AURORA_KEY_MIN_LENGTH 12u
#define AURORA_KEY_MAX_LENGTH 32u

static char credential_buffer[AURORA_KEY_MAX_LENGTH + 1u];
static size_t credential_length;
static bool create_offer_active;
static bool create_entry_mode;
static bool creation_notice_active;
static bool session_active_announced;
static bool logout_in_progress;
static bool unlock_failed_notice;

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
    session_manager_client_pump();

    enum aurora_session_manager_client_state session_state =
        session_manager_client_state();
    enum aurora_identity_client_state identity_state =
        identity_client_state();

    if (session_state == AURORA_SESSION_CLIENT_ACTIVE) {
        if (!user_session_host_active()) {
            if (!user_session_host_start()) {
                (void)session_manager_client_terminate();
                login_ui_set_state(AURORA_LOGIN_TERMINATING);
                log_line("[user-session] bootstrap/runtime failure; session termination requested");
                return;
            }
            /* Verify a real post-bootstrap Ring 3 IPC round-trip before
             * presenting the authenticated session as operational. */
            if (!user_session_host_health_check()) {
                (void)user_session_host_stop();
                (void)session_manager_client_terminate();
                login_ui_set_state(AURORA_LOGIN_TERMINATING);
                log_line("[g5-shell] production Ring 3 IPC health failed; session revoked");
                return;
            }
            log_line("[user-session] Ring 3 host and G5 IPC health operational");
        }

        clear_credential();
        create_offer_active = false;
        create_entry_mode = false;
        creation_notice_active = false;
        logout_in_progress = false;
        unlock_failed_notice = false;
        login_ui_set_state(AURORA_LOGIN_SESSION_ACTIVE);
        if (!session_active_announced) {
            session_active_announced = true;
            log_write("[session-manager] authenticated session active; generation ");
            log_u64(session_manager_client_generation());
            log_line("");
        }
        return;
    }

    if (session_state == AURORA_SESSION_CLIENT_LOCKING) {
        login_ui_set_state(AURORA_LOGIN_LOCKING);
        return;
    }

    if (session_state == AURORA_SESSION_CLIENT_UNLOCKING) {
        login_ui_set_state(AURORA_LOGIN_UNLOCKING);
        return;
    }

    if (session_state == AURORA_SESSION_CLIENT_LOGGING_OUT) {
        login_ui_set_state(AURORA_LOGIN_LOGGING_OUT);
        return;
    }

    if (session_state == AURORA_SESSION_CLIENT_TERMINATING) {
        if (user_session_host_active()) {
            (void)user_session_host_stop();
        }
        clear_credential();
        login_ui_set_state(AURORA_LOGIN_TERMINATING);
        return;
    }

    if (session_state == AURORA_SESSION_CLIENT_TERMINATED) {
        if (user_session_host_active()) {
            (void)user_session_host_stop();
        }
        clear_credential();
        session_active_announced = false;
        logout_in_progress = false;
        unlock_failed_notice = false;
        identity_client_reset_result();
        login_ui_set_state(AURORA_LOGIN_TERMINATED);
        return;
    }

    if (session_state == AURORA_SESSION_CLIENT_READY && logout_in_progress) {
        logout_in_progress = false;
        session_active_announced = false;
        unlock_failed_notice = false;
        clear_credential();
        identity_client_reset_result();
        login_ui_set_state(AURORA_LOGIN_IDLE);
        log_line("[session-manager] logout complete; session-scoped profile authority revoked");
        return;
    }

    if (session_state == AURORA_SESSION_CLIENT_LOCKED) {
        create_offer_active = false;
        create_entry_mode = false;
        creation_notice_active = false;

        if (identity_state == AURORA_IDENTITY_CLIENT_VERIFIED) {
            uint8_t grant[AURORA_IDENTITY_SERVICE_GRANT_TOKEN_SIZE];
            for (size_t i = 0u; i < sizeof(grant); ++i) grant[i] = 0u;

            bool transferred =
                identity_client_take_session_grant(grant) &&
                session_manager_client_unlock(grant);

            for (size_t i = 0u; i < sizeof(grant); ++i) grant[i] = 0u;
            clear_credential();

            if (!transferred) {
                unlock_failed_notice = true;
                login_ui_set_state(AURORA_LOGIN_ERROR);
                return;
            }

            unlock_failed_notice = false;
            login_ui_set_state(AURORA_LOGIN_UNLOCKING);
            return;
        }

        if (identity_state == AURORA_IDENTITY_CLIENT_AUTH_FAILED) {
            clear_credential();
            unlock_failed_notice = true;
            identity_client_reset_result();
            login_ui_set_state(AURORA_LOGIN_ERROR);
            return;
        }

        if (identity_state == AURORA_IDENTITY_CLIENT_THROTTLED) {
            clear_credential();
            unlock_failed_notice = true;
            identity_client_reset_result();
            login_ui_set_state(AURORA_LOGIN_THROTTLED);
            return;
        }

        if (identity_state == AURORA_IDENTITY_CLIENT_AUTHENTICATING) {
            login_ui_set_state(AURORA_LOGIN_UNLOCKING);
            return;
        }

        if (identity_state == AURORA_IDENTITY_CLIENT_UNAVAILABLE ||
            identity_state == AURORA_IDENTITY_CLIENT_ERROR) {
            clear_credential();
            unlock_failed_notice = true;
            login_ui_set_state(AURORA_LOGIN_ERROR);
            return;
        }

        if (!unlock_failed_notice) {
            login_ui_set_state(AURORA_LOGIN_LOCKED);
        }
        return;
    }

    if (session_state == AURORA_SESSION_CLIENT_STARTING) {
        login_ui_set_state(AURORA_LOGIN_AUTHENTICATING);
        return;
    }

    if (session_state == AURORA_SESSION_CLIENT_REJECTED ||
        session_state == AURORA_SESSION_CLIENT_UNAVAILABLE ||
        session_state == AURORA_SESSION_CLIENT_ERROR) {
        clear_credential();
        login_ui_set_state(AURORA_LOGIN_ERROR);
        return;
    }

    switch (identity_state) {
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

        case AURORA_IDENTITY_CLIENT_VERIFIED: {
            uint8_t grant[AURORA_IDENTITY_SERVICE_GRANT_TOKEN_SIZE];
            for (size_t i = 0u; i < sizeof(grant); ++i) grant[i] = 0u;

            bool transferred =
                identity_client_take_session_grant(grant) &&
                session_manager_client_begin(grant);

            for (size_t i = 0u; i < sizeof(grant); ++i) grant[i] = 0u;

            clear_credential();
            create_offer_active = false;
            create_entry_mode = false;
            creation_notice_active = false;

            if (!transferred) {
                login_ui_set_state(AURORA_LOGIN_ERROR);
                return;
            }

            login_ui_set_state(AURORA_LOGIN_AUTHENTICATING);
            return;
        }

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
    enum aurora_session_manager_client_state session_state =
        session_manager_client_state();

    if (session_state == AURORA_SESSION_CLIENT_TERMINATED) {
        if (key == AURORA_KEY_ENTER || key == AURORA_KEY_ESCAPE) {
            clear_credential();
            identity_client_reset_result();
            if (session_manager_client_acknowledge_terminated()) {
                login_ui_set_state(AURORA_LOGIN_IDLE);
                log_line("[session-manager] terminated session acknowledged; pre-session login restored");
            } else {
                login_ui_set_state(AURORA_LOGIN_ERROR);
            }
        }
        return;
    }

    if (session_state == AURORA_SESSION_CLIENT_ACTIVE) {
        if (key == AURORA_KEY_ENTER) {
            bool host_stopped =
                !user_session_host_active() ||
                user_session_host_stop();
            if (host_stopped && session_manager_client_lock()) {
                clear_credential();
                unlock_failed_notice = false;
                login_ui_set_state(AURORA_LOGIN_LOCKING);
            } else {
                login_ui_set_state(AURORA_LOGIN_ERROR);
            }
        } else if (key == AURORA_KEY_ESCAPE) {
            bool host_stopped =
                !user_session_host_active() ||
                user_session_host_stop();
            if (host_stopped && session_manager_client_logout()) {
                logout_in_progress = true;
                login_ui_set_state(AURORA_LOGIN_LOGGING_OUT);
            } else {
                login_ui_set_state(AURORA_LOGIN_ERROR);
            }
        }
        return;
    }

    if (session_state == AURORA_SESSION_CLIENT_LOCKED) {
        if (state == AURORA_IDENTITY_CLIENT_AUTHENTICATING ||
            state == AURORA_IDENTITY_CLIENT_VERIFIED) {
            return;
        }

        if (key == AURORA_KEY_ESCAPE) {
            clear_credential();
            unlock_failed_notice = false;
            identity_client_reset_result();
            if (session_manager_client_logout()) {
                logout_in_progress = true;
                login_ui_set_state(AURORA_LOGIN_LOGGING_OUT);
            } else {
                login_ui_set_state(AURORA_LOGIN_ERROR);
            }
            return;
        }

        if (key == AURORA_KEY_BACKSPACE) {
            unlock_failed_notice = false;
            if (credential_length != 0u) {
                --credential_length;
                credential_buffer[credential_length] = '\0';
                login_ui_set_masked_length(credential_length);
            }
            identity_client_reset_result();
            login_ui_set_state(AURORA_LOGIN_LOCKED);
            return;
        }

        if (key == AURORA_KEY_ENTER) {
            unlock_failed_notice = false;
            if (credential_length < AURORA_KEY_MIN_LENGTH) {
                login_ui_set_state(AURORA_LOGIN_ERROR);
                unlock_failed_notice = true;
                return;
            }

            bool submitted = identity_client_begin_key_auth(
                credential_buffer,
                credential_length);
            clear_credential();

            if (!submitted) {
                unlock_failed_notice = true;
                login_ui_set_state(AURORA_LOGIN_ERROR);
                return;
            }

            login_ui_set_state(AURORA_LOGIN_UNLOCKING);
            return;
        }

        char normalized = key_to_normalized_character(key);
        if (normalized == '\0' ||
            credential_length >= AURORA_KEY_MAX_LENGTH) {
            return;
        }

        unlock_failed_notice = false;
        identity_client_reset_result();
        credential_buffer[credential_length++] = normalized;
        credential_buffer[credential_length] = '\0';
        login_ui_set_masked_length(credential_length);
        login_ui_set_state(AURORA_LOGIN_LOCKED);
        return;
    }

    if (state == AURORA_IDENTITY_CLIENT_AUTHENTICATING ||
        state == AURORA_IDENTITY_CLIENT_VERIFIED ||
        state == AURORA_IDENTITY_CLIENT_CREATING ||
        session_state == AURORA_SESSION_CLIENT_STARTING ||
        session_state == AURORA_SESSION_CLIENT_LOCKING ||
        session_state == AURORA_SESSION_CLIENT_UNLOCKING ||
        session_state == AURORA_SESSION_CLIENT_LOGGING_OUT ||
        session_state == AURORA_SESSION_CLIENT_TERMINATING) {
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

    if (!identity_session_grant_ring3_self_test()) {
        kernel_panic("Ring 3 Identity session-grant consume self-test failed");
    }

    log_line("[identity-session] capability-authorized grant-consume gate + replay-safe rejection path passed");

    if (!identity_reauth_ring3_self_test()) {
        kernel_panic("Ring 3 Identity re-authentication self-test failed");
    }

    log_line("[identity-reauth] capability-gated fresh-auth flow + degraded fail-closed path passed");

    if (!identity_key_rotation_ring3_self_test()) {
        kernel_panic("Ring 3 Identity key-rotation self-test failed");
    }

    log_line("[identity-rotation] proof-gated manage-self capability + invalid-proof fail-closed path passed");

    if (!session_manager_ring3_self_test()) {
        kernel_panic("Ring 3 Session Manager service-to-service self-test failed");
    }

    log_line("[session-manager] Ring 3 service-to-service Identity binding + degraded fail-closed path passed");

    if (!user_session_host_self_test()) {
        kernel_panic("Ring 3 User Session Host profile-capability bootstrap self-test failed");
    }

    log_line("[user-session] Ring 3 profile capability bootstrap + teardown revocation passed");
    log_line("[g5-shell] authenticated Ring3 SCENE_PUBLISH reached compositor display");
    log_line("[g5-wp03] Ring3 Shell clean stop, fresh reauth, forced crash and capability revocation passed");

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
#if AURORA_BOOT_VALIDATION
    if (!display_ring3_self_test()) {
        kernel_panic("Ring 3 Display Service acceptance probe failed");
    }
    log_line("[display-ring3] capability-gated repeated present acceptance gate passed");
#endif

#if AURORA_BOOT_VALIDATION
    if (!graphics_ring3_two_client_self_test()) {
        kernel_panic("G2 two-client Ring 3 graphics acceptance probe failed");
    }
    log_line("[graphics-ring3] two-client syscall isolation + frame callback acceptance gate passed");
    log_line("[g5-graphics] Ring3 committed surfaces composed and presented to display");
#endif

    log_write("[boot-perf] storage init start at ");
    log_u64(clock_now_ns() / UINT64_C(1000000));
    log_line(" ms");

    bootstrap_storage_probe();
    nvme_bootstrap_probe();

    log_write("[boot-perf] storage discovery complete at ");
    log_u64(clock_now_ns() / UINT64_C(1000000));
    log_line(" ms");

    struct aurora_block_device *nvme = nvme_namespace_block_device();
    if (nvme != NULL && !nvme_namespace_reserved_for_rw_probe()) {
        bootstrap_storage_probe_device(nvme, "NVMe");
    } else if (nvme != NULL) {
        log_line("[nvme] dedicated reversible-probe media; filesystem bootstrap skipped");
    }

#if AURORA_BOOT_VALIDATION
    if (!ipc_wait_ring3_self_test()) {
        kernel_panic("Ring 3 IPC blocking wait/wakeup self-test failed");
    }

    log_line("[ring3-ipc] blocking wait/wakeup syscall probe passed");
    if (!g5_ipc_ring3_self_test()) {
        kernel_panic("G5 Ring 3 IPC frame roundtrip test failed");
    }
    log_line("[g5-ipc] isolated Ring 3 frame roundtrip passed");

    if (!security_activity_model_self_test()) {
        kernel_panic("Security Activity presentation model self-test failed");
    }
    log_line("[identity-activity] headless presentation model acceptance gate passed");

    if (!security_activity_renderer_self_test()) {
        kernel_panic("Security Activity renderer self-test failed");
    }
    log_line("[identity-activity] renderer acceptance gate passed");

#if AURORA_BOOT_VALIDATION
    if (!security_activity_graphics_self_test()) {
        kernel_panic("Security Activity native artwork self-test failed");
    }
    log_line("[identity-activity] native artwork catalog acceptance gate passed");
#endif

    if (entropy_ready()) {
        if (!entropy_ring3_self_test()) {
            kernel_panic("Ring 3 entropy seed syscall self-test failed");
        }
        log_line("[ring3-entropy] capability-gated trusted seed syscall probe passed");
    } else {
        struct aurora_entropy_status entropy_diagnostic = entropy_get_status();
        log_line("[ring3-entropy] trusted seed unavailable; capability probe skipped");
        log_write("[ring3-entropy] health_failed=");
        log_u64(entropy_diagnostic.health_failed ? 1u : 0u);
        log_write(" source_failures=");
        log_u64(entropy_diagnostic.source_failures);
        log_write(" health_failures=");
        log_u64(entropy_diagnostic.health_failures);
        log_write(" startup_samples=");
        log_u64(entropy_diagnostic.startup_samples);
        log_write(" output_words=");
        log_u64(entropy_diagnostic.output_words);
        log_line("");
        /* Validation builds must never certify an unexecuted security gate. */
        kernel_panic("Ring 3 trusted entropy unavailable in boot validation");
    }

    protected_state_bootstrap_probe();
    bool replay_recovered = false;
    if (!g5_ipc_durable_boot_probe(&replay_recovered)) {
        log_line("[g5-ipc] durable replay probe failed after stage:");
        log_u64(g5_ipc_durable_boot_probe_stage());
        log_line("");
        log_line("[g5-ipc] protected replace stage:");
        log_u64(protected_state_replace_debug_stage());
        log_line("");
        log_line("[g5-ipc] AuroraFS rename stage:");
        log_u64(aurora_fs_v2_rename_debug_stage());
        log_line("");
        kernel_panic("G5 durable IPC replay cold-boot probe failed");
    }
    if (replay_recovered) {
        log_line("[g5-ipc] durable ledger recovered from previous boot");
    } else {
        log_line("[g5-ipc] durable ledger initialized and persisted");
    }
#endif

    credential_length = 0u;
    create_offer_active = false;
    create_entry_mode = false;
    creation_notice_active = false;
    session_active_announced = false;
    logout_in_progress = false;
    unlock_failed_notice = false;

    for (size_t i = 0u; i < sizeof(credential_buffer); ++i) {
        credential_buffer[i] = '\0';
    }

    log_write("[boot-perf] Identity client start at ");
    log_u64(clock_now_ns() / UINT64_C(1000000));
    log_line(" ms");

    bool identity_ready = identity_client_init();
    bool session_manager_ready =
        identity_ready && session_manager_client_init();

    log_write("[boot-perf] Identity client ready at ");
    log_u64(clock_now_ns() / UINT64_C(1000000));
    log_line(" ms");

    /*
     * Decode and publish the final artwork exactly once, after the live
     * Identity service initialization. Until this point the boot splash stays
     * on screen, so the emergency fallback never flashes during a normal boot.
     */
    log_write("[boot-perf] Identity artwork decode start at ");
    log_u64(clock_now_ns() / UINT64_C(1000000));
    log_line(" ms");

    if (login_ui_activate_native_artwork()) {
        log_line("[identity-gui] full-quality PNG artwork active");
    } else {
        log_line("[identity-gui] native artwork unavailable; procedural fallback active");
        login_ui_render();
    }

    log_write("[boot-perf] Identity artwork ready at ");
    log_u64(clock_now_ns() / UINT64_C(1000000));
    log_line(" ms");

    if (identity_ready && session_manager_ready) {
        log_line("[identity-client] production Ring 3 Identity service connected to login input");
        log_line("[session-manager] production Ring 3 Session Manager ready");
        login_ui_set_state(AURORA_LOGIN_IDLE);
    } else {
        log_line("[identity-client] Identity/Session service unavailable; login remains fail-closed");
        login_ui_set_state(AURORA_LOGIN_ERROR);
    }

    log_write("[boot] Identity login ready at ");
    log_u64(clock_now_ns() / UINT64_C(1000000));
    log_line(" ms");
}

void login_input_pump(void) {
    synchronize_identity_state();

    struct aurora_input_event event;

    while (input_poll_event(&event)) {
        if (event.type != AURORA_INPUT_EVENT_KEY ||
            !event.pressed) {
            continue;
        }

        handle_pressed_key(event.key);
    }
}
