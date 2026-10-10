#include <aurora/identity_presentation.h>

struct aurora_identity_presentation_decision
identity_presentation_decide(
    enum aurora_session_manager_client_state session_state,
    bool session_host_live,
    uint64_t session_generation
) {
    struct aurora_identity_presentation_decision out = {
        .domain = AURORA_IDENTITY_PRESENTATION_QUARANTINE,
        .allow_desktop_input = false,
        .allow_credential_input = false,
        .allow_login_framebuffer = false,
    };

    /* A normal Ring3 process must never retain interactive ownership
     * during lock/logout/pre-session, even if it is still alive. */
    if (session_host_live) {
        if (session_state == AURORA_SESSION_CLIENT_ACTIVE &&
            session_generation != 0u) {
            out.domain = AURORA_IDENTITY_PRESENTATION_DESKTOP;
            out.allow_desktop_input = true;
        }
        return out;
    }

    switch (session_state) {
        case AURORA_SESSION_CLIENT_ACTIVE:
            /* Health/bootstrap failure: never accept a credential as
             * pre-session simply because the desktop host disappeared. */
            return out;

        case AURORA_SESSION_CLIENT_LOCKING:
        case AURORA_SESSION_CLIENT_LOCKED:
        case AURORA_SESSION_CLIENT_UNLOCKING:
            out.domain = AURORA_IDENTITY_PRESENTATION_LOCK;
            out.allow_login_framebuffer = true;
            out.allow_credential_input =
                session_state == AURORA_SESSION_CLIENT_LOCKED;
            return out;

        case AURORA_SESSION_CLIENT_UNINITIALIZED:
        case AURORA_SESSION_CLIENT_READY:
        case AURORA_SESSION_CLIENT_STARTING:
        case AURORA_SESSION_CLIENT_LOGGING_OUT:
        case AURORA_SESSION_CLIENT_TERMINATING:
        case AURORA_SESSION_CLIENT_TERMINATED:
        case AURORA_SESSION_CLIENT_REJECTED:
        case AURORA_SESSION_CLIENT_UNAVAILABLE:
        case AURORA_SESSION_CLIENT_ERROR:
            out.domain = AURORA_IDENTITY_PRESENTATION_PRE_SESSION;
            out.allow_login_framebuffer = true;
            out.allow_credential_input =
                session_state == AURORA_SESSION_CLIENT_READY ||
                session_state == AURORA_SESSION_CLIENT_REJECTED ||
                session_state == AURORA_SESSION_CLIENT_TERMINATED ||
                session_state == AURORA_SESSION_CLIENT_UNINITIALIZED;
            return out;
        default:
            return out; /* Unrecognized states fail closed. */
    }
}

bool identity_presentation_self_test(void) {
    struct aurora_identity_presentation_decision d;
    d = identity_presentation_decide(AURORA_SESSION_CLIENT_READY,false,0u);
    if (d.domain != AURORA_IDENTITY_PRESENTATION_PRE_SESSION ||
        !d.allow_credential_input || !d.allow_login_framebuffer ||
        d.allow_desktop_input) return false;
    d = identity_presentation_decide(AURORA_SESSION_CLIENT_STARTING,false,0u);
    if (d.allow_credential_input || d.allow_desktop_input ||
        !d.allow_login_framebuffer) return false;
    d = identity_presentation_decide(AURORA_SESSION_CLIENT_ACTIVE,true,17u);
    if (d.domain != AURORA_IDENTITY_PRESENTATION_DESKTOP ||
        !d.allow_desktop_input || d.allow_credential_input ||
        d.allow_login_framebuffer) return false;
    d = identity_presentation_decide(AURORA_SESSION_CLIENT_ACTIVE,true,0u);
    if (d.domain != AURORA_IDENTITY_PRESENTATION_QUARANTINE ||
        d.allow_desktop_input || d.allow_credential_input) return false;
    d = identity_presentation_decide(AURORA_SESSION_CLIENT_ACTIVE,false,17u);
    if (d.domain != AURORA_IDENTITY_PRESENTATION_QUARANTINE ||
        d.allow_desktop_input || d.allow_credential_input) return false;
    d = identity_presentation_decide(AURORA_SESSION_CLIENT_LOCKED,false,17u);
    if (d.domain != AURORA_IDENTITY_PRESENTATION_LOCK ||
        !d.allow_credential_input || !d.allow_login_framebuffer ||
        d.allow_desktop_input) return false;
    d = identity_presentation_decide(AURORA_SESSION_CLIENT_LOCKED,true,17u);
    if (d.domain != AURORA_IDENTITY_PRESENTATION_QUARANTINE ||
        d.allow_desktop_input || d.allow_credential_input ||
        d.allow_login_framebuffer) return false;
    d = identity_presentation_decide(AURORA_SESSION_CLIENT_UNLOCKING,false,17u);
    if (d.allow_credential_input || !d.allow_login_framebuffer) return false;
    d = identity_presentation_decide(AURORA_SESSION_CLIENT_READY,true,17u);
    if (d.domain != AURORA_IDENTITY_PRESENTATION_QUARANTINE ||
        d.allow_credential_input || d.allow_desktop_input) return false;
    d = identity_presentation_decide(AURORA_SESSION_CLIENT_LOGGING_OUT,false,17u);
    if (d.allow_credential_input || d.allow_desktop_input ||
        !d.allow_login_framebuffer) return false;
    d = identity_presentation_decide(
        (enum aurora_session_manager_client_state)255, false, 0u);
    return d.domain == AURORA_IDENTITY_PRESENTATION_QUARANTINE &&
           !d.allow_credential_input && !d.allow_desktop_input &&
           !d.allow_login_framebuffer;
}
