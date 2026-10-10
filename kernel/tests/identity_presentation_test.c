#include <assert.h>
#include <aurora/identity_presentation.h>

int main(void) {
    assert(identity_presentation_self_test());
    struct aurora_identity_presentation_decision d;

    /* No desktop process may receive a key while the lock owns input. */
    for (uint32_t state = AURORA_SESSION_CLIENT_LOCKING;
         state <= AURORA_SESSION_CLIENT_UNLOCKING; ++state) {
        d = identity_presentation_decide(
            (enum aurora_session_manager_client_state)state, true, 51u);
        assert(d.domain == AURORA_IDENTITY_PRESENTATION_QUARANTINE);
        assert(!d.allow_credential_input && !d.allow_desktop_input);
        assert(!d.allow_login_framebuffer);
    }

    /* No spoofed SESSION_ACTIVE state without its live owner or epoch. */
    for (uint32_t state = 0u;
         state <= AURORA_SESSION_CLIENT_ERROR; ++state) {
        for (uint32_t live = 0u; live <= 1u; ++live) {
            for (uint32_t generation = 0u; generation <= 1u; ++generation) {
                d = identity_presentation_decide(
                    (enum aurora_session_manager_client_state)state,
                    live != 0u, generation);
                if (d.allow_desktop_input)
                    assert(state == AURORA_SESSION_CLIENT_ACTIVE &&
                           live != 0u && generation != 0u);
                if (d.allow_credential_input)
                    assert(!live && state != AURORA_SESSION_CLIENT_ACTIVE &&
                           !d.allow_desktop_input && d.allow_login_framebuffer);
                if (live && state != AURORA_SESSION_CLIENT_ACTIVE)
                    assert(d.domain == AURORA_IDENTITY_PRESENTATION_QUARANTINE);
            }
        }
    }

    d = identity_presentation_decide(AURORA_SESSION_CLIENT_READY,false,0u);
    assert(d.allow_credential_input && d.allow_login_framebuffer);
    d = identity_presentation_decide(AURORA_SESSION_CLIENT_ACTIVE,true,7u);
    assert(d.allow_desktop_input && !d.allow_login_framebuffer);
    d = identity_presentation_decide(AURORA_SESSION_CLIENT_LOCKED,false,7u);
    assert(d.allow_credential_input && d.allow_login_framebuffer);
    d = identity_presentation_decide(AURORA_SESSION_CLIENT_ACTIVE,true,8u);
    assert(d.allow_desktop_input && !d.allow_credential_input);
    return 0;
}
