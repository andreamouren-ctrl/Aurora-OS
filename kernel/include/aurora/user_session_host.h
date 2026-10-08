#ifndef AURORA_USER_SESSION_HOST_H
#define AURORA_USER_SESSION_HOST_H

#include <stdbool.h>
#include <aurora/g5_ipc_dispatch.h>

bool user_session_host_start(void);
bool user_session_host_stop(void);
bool user_session_host_active(void);

/* Registration is for a future session-scoped G5 service dispatcher.
 * This host owns only the lifecycle, not a Shell IPC endpoint. */
bool user_session_host_register_g5_dispatcher(struct g5_dispatch_context *dispatcher);

bool user_session_host_self_test(void);

#endif
