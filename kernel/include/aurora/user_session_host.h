#ifndef AURORA_USER_SESSION_HOST_H
#define AURORA_USER_SESSION_HOST_H

#include <stdbool.h>

bool user_session_host_start(void);
bool user_session_host_stop(void);
bool user_session_host_active(void);

bool user_session_host_self_test(void);

#endif
