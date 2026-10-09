#ifndef AURORA_SECURITY_ACTIVITY_APP_H
#define AURORA_SECURITY_ACTIVITY_APP_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/boot.h>
#include <aurora/security_activity_controller.h>
#include <aurora/security_activity_renderer.h>

enum aurora_security_activity_app_action {
    AURORA_SECURITY_ACTIVITY_APP_ACTION_NONE = 0,
    AURORA_SECURITY_ACTIVITY_APP_ACTION_FILTER_ALL,
    AURORA_SECURITY_ACTIVITY_APP_ACTION_FILTER_AUTH,
    AURORA_SECURITY_ACTIVITY_APP_ACTION_FILTER_SESSION,
    AURORA_SECURITY_ACTIVITY_APP_ACTION_FILTER_CREDENTIAL,
    AURORA_SECURITY_ACTIVITY_APP_ACTION_LOAD_MORE,
    AURORA_SECURITY_ACTIVITY_APP_ACTION_REFRESH,
    AURORA_SECURITY_ACTIVITY_APP_ACTION_CLOSE
};

struct aurora_security_activity_app {
    bool active;
    uint64_t revision;
    struct aurora_security_activity_controller controller;
    struct aurora_security_activity_render_state render;
};

void security_activity_app_init(
    struct aurora_security_activity_app *app);

bool security_activity_app_open(
    struct aurora_security_activity_app *app);

void security_activity_app_close(
    struct aurora_security_activity_app *app);

void security_activity_app_pump(
    struct aurora_security_activity_app *app);

bool security_activity_app_apply_action(
    struct aurora_security_activity_app *app,
    enum aurora_security_activity_app_action action);

bool security_activity_app_render(
    const struct aurora_security_activity_app *app,
    const struct aurora_framebuffer *framebuffer);

#endif
