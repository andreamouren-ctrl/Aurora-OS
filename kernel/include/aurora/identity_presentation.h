#ifndef AURORA_IDENTITY_PRESENTATION_H
#define AURORA_IDENTITY_PRESENTATION_H

#include <stdbool.h>
#include <stdint.h>
#include <aurora/session_manager_client.h>

/* This is trusted *routing policy*, not an authentication decision.
 * No compositor/client-supplied state can elevate a session to ACTIVE. */
enum aurora_identity_presentation_domain {
    AURORA_IDENTITY_PRESENTATION_PRE_SESSION = 0,
    AURORA_IDENTITY_PRESENTATION_DESKTOP,
    AURORA_IDENTITY_PRESENTATION_LOCK,
    AURORA_IDENTITY_PRESENTATION_QUARANTINE
};

struct aurora_identity_presentation_decision {
    enum aurora_identity_presentation_domain domain;
    bool allow_desktop_input;
    bool allow_credential_input;
    bool allow_login_framebuffer;
};

struct aurora_identity_presentation_decision
identity_presentation_decide(
    enum aurora_session_manager_client_state session_state,
    bool session_host_live,
    uint64_t session_generation
);

/* Pure contract self-test, including forbidden mixed-trust transitions. */
bool identity_presentation_self_test(void);

#endif
