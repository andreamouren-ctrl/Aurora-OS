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

    /* A second desktop generation is a new trust epoch even if its visual
     * input domain and flags are identical. No old Ring 3 key may cross. */
    const struct aurora_identity_presentation_decision desktop7 =
        identity_presentation_decide(AURORA_SESSION_CLIENT_ACTIVE, true, 7u);
    const struct aurora_identity_presentation_decision desktop8 =
        identity_presentation_decide(AURORA_SESSION_CLIENT_ACTIVE, true, 8u);
    assert(identity_presentation_same_input_epoch(&desktop7, 7u, &desktop7, 7u));
    assert(!identity_presentation_same_input_epoch(&desktop7, 7u, &desktop8, 8u));

    /* The generation fence also applies to reauthentication/lock queues. */
    const struct aurora_identity_presentation_decision locked7 =
        identity_presentation_decide(AURORA_SESSION_CLIENT_LOCKED, false, 7u);
    const struct aurora_identity_presentation_decision locked8 =
        identity_presentation_decide(AURORA_SESSION_CLIENT_LOCKED, false, 8u);
    assert(!identity_presentation_same_input_epoch(&locked7, 7u, &locked8, 8u));
    assert(!identity_presentation_same_input_epoch(&desktop7, 7u, &locked7, 7u));

    /* A READY -> STARTING policy change removes credential input even
     * though the coarse PRE_SESSION domain stays unchanged. */
    const struct aurora_identity_presentation_decision ready =
        identity_presentation_decide(AURORA_SESSION_CLIENT_READY, false, 0u);
    const struct aurora_identity_presentation_decision starting =
        identity_presentation_decide(AURORA_SESSION_CLIENT_STARTING, false, 0u);
    assert(ready.domain == starting.domain);
    assert(!identity_presentation_same_input_epoch(&ready, 0u, &starting, 0u));
    assert(identity_presentation_same_input_epoch(&ready, 0u, &ready, 0u));
    assert(!identity_presentation_same_input_epoch(NULL, 0u, &ready, 0u));
    assert(!identity_presentation_same_input_epoch(&ready, 0u, NULL, 0u));

    return 0;
}
