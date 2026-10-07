#include <stddef.h>
#include <stdint.h>

#include <aurora/display_link_training.h>

static enum aurora_link_training_state first_state(
    enum aurora_link_kind kind
) {
    switch (kind) {
        case AURORA_LINK_DISPLAYPORT:
            return AURORA_LINK_TRAINING_CLOCK_RECOVERY;
        case AURORA_LINK_HDMI_TMDS:
            return AURORA_LINK_TRAINING_ACTIVE;
        case AURORA_LINK_HDMI_FRL:
            return AURORA_LINK_TRAINING_FRL_PATTERN;
        default:
            return AURORA_LINK_TRAINING_FAILED;
    }
}

static enum aurora_link_training_state next_state(
    enum aurora_link_kind kind,
    enum aurora_link_training_state state
) {
    if (kind == AURORA_LINK_DISPLAYPORT) {
        if (state == AURORA_LINK_TRAINING_CLOCK_RECOVERY) {
            return AURORA_LINK_TRAINING_CHANNEL_EQUALIZATION;
        }
        if (state == AURORA_LINK_TRAINING_CHANNEL_EQUALIZATION) {
            return AURORA_LINK_TRAINING_ACTIVE;
        }
    }

    if (kind == AURORA_LINK_HDMI_FRL &&
        state == AURORA_LINK_TRAINING_FRL_PATTERN) {
        return AURORA_LINK_TRAINING_ACTIVE;
    }

    if (kind == AURORA_LINK_HDMI_TMDS &&
        state == AURORA_LINK_TRAINING_ACTIVE) {
        return AURORA_LINK_TRAINING_ACTIVE;
    }

    return AURORA_LINK_TRAINING_FAILED;
}

bool display_link_training_start(
    struct aurora_link_training_session *session,
    enum aurora_link_kind kind,
    uint32_t max_attempts
) {
    if (session == NULL ||
        kind == AURORA_LINK_NONE ||
        max_attempts == 0u ||
        max_attempts > 32u) {
        return false;
    }

    session->kind = kind;
    session->state = AURORA_LINK_TRAINING_PREPARE;
    session->attempts = 0u;
    session->max_attempts = max_attempts;
    return true;
}

bool display_link_training_run(
    struct aurora_link_training_session *session,
    const struct aurora_link_training_backend *backend
) {
    if (session == NULL ||
        backend == NULL ||
        backend->step == NULL ||
        session->state != AURORA_LINK_TRAINING_PREPARE) {
        return false;
    }

    if (!backend->step(
            backend->context,
            AURORA_LINK_TRAINING_PREPARE,
            0u)) {
        session->state = AURORA_LINK_TRAINING_FAILED;
        return false;
    }

    enum aurora_link_training_state state =
        first_state(session->kind);

    if (state == AURORA_LINK_TRAINING_FAILED) {
        session->state = state;
        return false;
    }

    if (state == AURORA_LINK_TRAINING_ACTIVE) {
        session->state = state;
        return backend->step(
            backend->context,
            AURORA_LINK_TRAINING_ACTIVE,
            0u
        );
    }

    while (state != AURORA_LINK_TRAINING_ACTIVE) {
        bool completed = false;

        for (uint32_t attempt = 1u;
             attempt <= session->max_attempts;
             ++attempt) {
            session->attempts = attempt;

            if (backend->step(
                    backend->context,
                    state,
                    attempt)) {
                completed = true;
                break;
            }
        }

        if (!completed) {
            session->state = AURORA_LINK_TRAINING_FAILED;
            return false;
        }

        state = next_state(session->kind, state);

        if (state == AURORA_LINK_TRAINING_FAILED) {
            session->state = state;
            return false;
        }
    }

    session->state = AURORA_LINK_TRAINING_ACTIVE;
    return backend->step(
        backend->context,
        AURORA_LINK_TRAINING_ACTIVE,
        session->attempts
    );
}

struct link_selftest_state {
    uint32_t prepare_calls;
    uint32_t cr_calls;
    uint32_t eq_calls;
    uint32_t frl_calls;
    uint32_t active_calls;
};

static bool selftest_step(
    void *context,
    enum aurora_link_training_state state,
    uint32_t attempt
) {
    struct link_selftest_state *probe =
        (struct link_selftest_state *)context;

    if (probe == NULL) return false;

    switch (state) {
        case AURORA_LINK_TRAINING_PREPARE:
            ++probe->prepare_calls;
            return true;

        case AURORA_LINK_TRAINING_CLOCK_RECOVERY:
            ++probe->cr_calls;
            return attempt >= 2u;

        case AURORA_LINK_TRAINING_CHANNEL_EQUALIZATION:
            ++probe->eq_calls;
            return true;

        case AURORA_LINK_TRAINING_FRL_PATTERN:
            ++probe->frl_calls;
            return attempt >= 2u;

        case AURORA_LINK_TRAINING_ACTIVE:
            ++probe->active_calls;
            return true;

        default:
            return false;
    }
}

bool display_link_training_selftest(void) {
    struct aurora_link_training_backend backend;
    struct link_selftest_state dp_state = {0};

    backend.step = selftest_step;
    backend.context = &dp_state;

    struct aurora_link_training_session dp;

    if (!display_link_training_start(
            &dp,
            AURORA_LINK_DISPLAYPORT,
            4u) ||
        !display_link_training_run(&dp, &backend) ||
        dp.state != AURORA_LINK_TRAINING_ACTIVE ||
        dp_state.prepare_calls != 1u ||
        dp_state.cr_calls != 2u ||
        dp_state.eq_calls != 1u ||
        dp_state.active_calls != 1u) {
        return false;
    }

    struct link_selftest_state hdmi_state = {0};
    backend.context = &hdmi_state;

    struct aurora_link_training_session hdmi;

    return display_link_training_start(
            &hdmi,
            AURORA_LINK_HDMI_FRL,
            4u) &&
        display_link_training_run(&hdmi, &backend) &&
        hdmi.state == AURORA_LINK_TRAINING_ACTIVE &&
        hdmi_state.prepare_calls == 1u &&
        hdmi_state.frl_calls == 2u &&
        hdmi_state.active_calls == 1u;
}
