#include <stddef.h>
#include <aurora/identity_client.h>
#include <aurora/security_activity_controller.h>

void security_activity_controller_init(
    struct aurora_security_activity_controller *controller
) {
    if (controller == NULL) return;
    *controller = (struct aurora_security_activity_controller){0};
    controller->state = AURORA_SECURITY_ACTIVITY_VIEW_IDLE;
    security_activity_page_init(&controller->page);
}

static bool begin_read(
    struct aurora_security_activity_controller *controller,
    uint64_t before_sequence
) {
    if (controller == NULL ||
        controller->state == AURORA_SECURITY_ACTIVITY_VIEW_LOADING) {
        return false;
    }

    /* A rejected request must not destroy a previously displayed page.
     * The shared Identity client may be occupied by authentication. */
    enum aurora_identity_client_state initial_state = identity_client_state();
    if (!identity_client_begin_security_activity_read(before_sequence)) {
        controller->state = AURORA_SECURITY_ACTIVITY_VIEW_ERROR;
        /* Recover only an error produced by this attempted read.
         * Do not clear another operation's pending or terminal result. */
        if (initial_state == AURORA_IDENTITY_CLIENT_READY &&
            identity_client_state() == AURORA_IDENTITY_CLIENT_ERROR) {
            identity_client_reset_result();
        }
        return false;
    }

    security_activity_page_init(&controller->page);
    controller->cursor = before_sequence;
    controller->reached_end = false;
    controller->state = AURORA_SECURITY_ACTIVITY_VIEW_LOADING;
    return true;
}

bool security_activity_controller_begin(
    struct aurora_security_activity_controller *controller
) {
    return begin_read(controller, 0u);
}

void security_activity_controller_pump(
    struct aurora_security_activity_controller *controller
) {
    if (controller == NULL ||
        controller->state != AURORA_SECURITY_ACTIVITY_VIEW_LOADING) {
        return;
    }

    identity_client_pump();
    enum aurora_identity_client_state state = identity_client_state();

    if (state == AURORA_IDENTITY_CLIENT_READING_ACTIVITY) return;

    if (state == AURORA_IDENTITY_CLIENT_ACTIVITY_END) {
        controller->reached_end = true;
        controller->page.has_more = false;
        controller->state =
            controller->page.count == 0u
                ? (controller->cursor == 0u
                    ? AURORA_SECURITY_ACTIVITY_VIEW_EMPTY
                    : AURORA_SECURITY_ACTIVITY_VIEW_END)
                : AURORA_SECURITY_ACTIVITY_VIEW_READY;
        (void)identity_client_discard_completed_security_activity();
        return;
    }

    if (state == AURORA_IDENTITY_CLIENT_ACTIVITY_RECORD) {
        struct aurora_security_activity_record record;
        if (!identity_client_take_security_activity_record(&record) ||
            (controller->cursor != 0u && record.sequence >= controller->cursor) ||
            !security_activity_page_append(&controller->page, &record)) {
            controller->state = AURORA_SECURITY_ACTIVITY_VIEW_ERROR;
            (void)identity_client_discard_completed_security_activity();
            return;
        }

        controller->cursor = record.sequence;
        if (controller->page.count >= AURORA_SECURITY_ACTIVITY_PAGE_CAPACITY) {
            controller->page.has_more = true;
            controller->state = AURORA_SECURITY_ACTIVITY_VIEW_READY;
            return;
        }

        if (!identity_client_begin_security_activity_read(controller->cursor)) {
            controller->state = AURORA_SECURITY_ACTIVITY_VIEW_ERROR;
            /* A transport/send failure may leave the client in ERROR.
             * Do not reset READY or any unrelated auth/reauth result. */
            if (identity_client_state() == AURORA_IDENTITY_CLIENT_ERROR) {
                identity_client_reset_result();
            }
        }
        return;
    }

    if (state == AURORA_IDENTITY_CLIENT_UNAVAILABLE ||
        state == AURORA_IDENTITY_CLIENT_ERROR) {
        controller->state = AURORA_SECURITY_ACTIVITY_VIEW_ERROR;
        identity_client_reset_result();
        return;
    }

    /* The client is shared with login and re-authentication. Never consume
     * an unexpected non-activity result: it may carry another caller's grant. */
    controller->state = AURORA_SECURITY_ACTIVITY_VIEW_ERROR;
}

bool security_activity_controller_next_page(
    struct aurora_security_activity_controller *controller
) {
    if (controller == NULL ||
        controller->state != AURORA_SECURITY_ACTIVITY_VIEW_READY ||
        controller->reached_end ||
        controller->page.count == 0u ||
        controller->page.next_before_sequence == 0u) {
        return false;
    }

    return begin_read(controller, controller->page.next_before_sequence);
}
