#include <stddef.h>

#include <aurora/security_activity_app.h>

static void bump_revision(struct aurora_security_activity_app *app) {
    if (app == NULL) return;
    ++app->revision;
    if (app->revision == 0u) app->revision = 1u;
}

void security_activity_app_init(
    struct aurora_security_activity_app *app
) {
    if (app == NULL) return;
    *app = (struct aurora_security_activity_app){0};
    security_activity_controller_init(&app->controller);
    app->render.filter = AURORA_SECURITY_ACTIVITY_FILTER_ALL;
    app->render.focused_load_more = false;
}

bool security_activity_app_open(
    struct aurora_security_activity_app *app
) {
    if (app == NULL) return false;

    security_activity_controller_init(&app->controller);
    app->render.filter = AURORA_SECURITY_ACTIVITY_FILTER_ALL;
    app->render.focused_load_more = false;
    app->active = true;
    bump_revision(app);

    /*
     * Failure to start the read is a presentation ERROR state, not a reason
     * to bypass or replace the Identity path. The app stays open so the user
     * can see the error and explicitly retry.
     */
    (void)security_activity_controller_begin(&app->controller);
    return true;
}

void security_activity_app_close(
    struct aurora_security_activity_app *app
) {
    if (app == NULL) return;

    app->active = false;
    security_activity_controller_init(&app->controller);
    app->render.filter = AURORA_SECURITY_ACTIVITY_FILTER_ALL;
    app->render.focused_load_more = false;
    security_activity_renderer_reset_native();
    bump_revision(app);
}

void security_activity_app_pump(
    struct aurora_security_activity_app *app
) {
    if (app == NULL || !app->active) return;

    enum aurora_security_activity_view_state previous_state =
        app->controller.state;
    size_t previous_count = app->controller.page.count;
    uint64_t previous_cursor = app->controller.cursor;

    security_activity_controller_pump(&app->controller);

    if (previous_state != app->controller.state ||
        previous_count != app->controller.page.count ||
        previous_cursor != app->controller.cursor) {
        bump_revision(app);
    }
}

static bool set_filter(
    struct aurora_security_activity_app *app,
    enum aurora_security_activity_filter filter
) {
    if (app == NULL || !app->active ||
        filter > AURORA_SECURITY_ACTIVITY_FILTER_CREDENTIAL) {
        return false;
    }

    if (app->render.filter == filter) return true;
    app->render.filter = filter;
    bump_revision(app);
    return true;
}

bool security_activity_app_apply_action(
    struct aurora_security_activity_app *app,
    enum aurora_security_activity_app_action action
) {
    if (app == NULL || !app->active) return false;

    switch (action) {
        case AURORA_SECURITY_ACTIVITY_APP_ACTION_FILTER_ALL:
            return set_filter(app, AURORA_SECURITY_ACTIVITY_FILTER_ALL);
        case AURORA_SECURITY_ACTIVITY_APP_ACTION_FILTER_AUTH:
            return set_filter(app, AURORA_SECURITY_ACTIVITY_FILTER_AUTH);
        case AURORA_SECURITY_ACTIVITY_APP_ACTION_FILTER_SESSION:
            return set_filter(app, AURORA_SECURITY_ACTIVITY_FILTER_SESSION);
        case AURORA_SECURITY_ACTIVITY_APP_ACTION_FILTER_CREDENTIAL:
            return set_filter(app, AURORA_SECURITY_ACTIVITY_FILTER_CREDENTIAL);

        case AURORA_SECURITY_ACTIVITY_APP_ACTION_LOAD_MORE:
            if (!security_activity_controller_next_page(&app->controller)) {
                return false;
            }
            app->render.focused_load_more = false;
            bump_revision(app);
            return true;

        case AURORA_SECURITY_ACTIVITY_APP_ACTION_REFRESH:
            if (app->controller.state ==
                AURORA_SECURITY_ACTIVITY_VIEW_LOADING) {
                return false;
            }
            if (!security_activity_controller_begin(&app->controller)) {
                /*
                 * begin() moves the controller to ERROR on client failure.
                 * That state must remain visible rather than being hidden.
                 */
                bump_revision(app);
                return false;
            }
            bump_revision(app);
            return true;

        case AURORA_SECURITY_ACTIVITY_APP_ACTION_CLOSE:
            security_activity_app_close(app);
            return true;

        case AURORA_SECURITY_ACTIVITY_APP_ACTION_NONE:
        default:
            return false;
    }
}

bool security_activity_app_render(
    const struct aurora_security_activity_app *app,
    const struct aurora_framebuffer *framebuffer
) {
    if (app == NULL || !app->active ||
        !security_activity_renderer_validate_framebuffer(framebuffer)) {
        return false;
    }

    /*
     * The host owns the surface/framebuffer. This adapter never allocates a
     * private display target, creates a window, or acquires compositor/display
     * authority. WP-04+ will supply the compositor-backed surface.
     */
    security_activity_renderer_draw(
        framebuffer,
        &app->controller,
        &app->render);
    return true;
}
