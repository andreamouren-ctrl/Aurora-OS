#include <stdint.h>

#include <aurora/security_activity_app.h>
#include <aurora/security_activity_app_probe.h>

bool security_activity_app_self_test(void) {
    struct aurora_security_activity_app app;
    security_activity_app_init(&app);

    if (app.active ||
        app.render.filter != AURORA_SECURITY_ACTIVITY_FILTER_ALL ||
        app.controller.state != AURORA_SECURITY_ACTIVITY_VIEW_IDLE) {
        return false;
    }

    /*
     * The probe exercises only pure application interaction. It deliberately
     * does not call open(), because opening performs a real Identity-client
     * read and must remain under the existing integration probes.
     */
    app.active = true;
    app.controller.state = AURORA_SECURITY_ACTIVITY_VIEW_READY;
    app.controller.page.count = 1u;
    app.controller.page.items[0].sequence = 9u;
    app.controller.page.next_before_sequence = 9u;
    app.controller.page.has_more = false;

    uint64_t revision = app.revision;
    if (!security_activity_app_apply_action(
            &app, AURORA_SECURITY_ACTIVITY_APP_ACTION_FILTER_AUTH) ||
        app.render.filter != AURORA_SECURITY_ACTIVITY_FILTER_AUTH ||
        app.revision == revision) {
        return false;
    }

    revision = app.revision;
    if (!security_activity_app_apply_action(
            &app, AURORA_SECURITY_ACTIVITY_APP_ACTION_FILTER_CREDENTIAL) ||
        app.render.filter != AURORA_SECURITY_ACTIVITY_FILTER_CREDENTIAL ||
        app.revision == revision) {
        return false;
    }

    if (security_activity_app_apply_action(
            &app, AURORA_SECURITY_ACTIVITY_APP_ACTION_LOAD_MORE)) {
        return false;
    }

    if (!security_activity_app_apply_action(
            &app, AURORA_SECURITY_ACTIVITY_APP_ACTION_CLOSE) ||
        app.active ||
        app.controller.state != AURORA_SECURITY_ACTIVITY_VIEW_IDLE ||
        app.render.filter != AURORA_SECURITY_ACTIVITY_FILTER_ALL) {
        return false;
    }

    return !security_activity_app_apply_action(
        &app, AURORA_SECURITY_ACTIVITY_APP_ACTION_FILTER_ALL);
}
