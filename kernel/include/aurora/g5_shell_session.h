#ifndef AURORA_G5_SHELL_SESSION_H
#define AURORA_G5_SHELL_SESSION_H
#include <aurora/g5_session_context.h>
#include <stdbool.h>
#include <stdint.h>
/* Serialized trusted lifecycle bridge; no IPC payload may call these directly. */
struct g5_shell_session {
 struct g5_session_context context;
 uint64_t last_ready_generation;
 uint64_t revocations;
};
bool g5_shell_session_ready(struct g5_shell_session *s,uint64_t generation);
bool g5_shell_session_check(const struct g5_shell_session *s,uint64_t generation);
void g5_shell_session_end(struct g5_shell_session *s);
#endif
