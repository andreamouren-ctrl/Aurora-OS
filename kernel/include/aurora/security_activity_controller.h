#ifndef AURORA_SECURITY_ACTIVITY_CONTROLLER_H
#define AURORA_SECURITY_ACTIVITY_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/security_activity_model.h>

struct aurora_security_activity_controller {
    enum aurora_security_activity_view_state state;
    struct aurora_security_activity_page page;
    uint64_t cursor;
    bool reached_end;
};

void security_activity_controller_init(
    struct aurora_security_activity_controller *controller);

bool security_activity_controller_begin(
    struct aurora_security_activity_controller *controller);

void security_activity_controller_pump(
    struct aurora_security_activity_controller *controller);

bool security_activity_controller_next_page(
    struct aurora_security_activity_controller *controller);

#endif
