#include <aurora/g5_shell_session.h>
#include <stddef.h>
#include <stdint.h>
bool g5_shell_session_ready(struct g5_shell_session *s,uint64_t generation) {
 if(!s || !generation || s->context.active ||
    generation<=s->last_ready_generation)return false;
 if(!g5_session_context_begin(&s->context,generation))return false;
 s->last_ready_generation=generation;
 return true;
}
bool g5_shell_session_check(const struct g5_shell_session *s,uint64_t generation) {
 return s && g5_session_context_authorized(&s->context,generation) &&
  s->last_ready_generation==generation;
}
void g5_shell_session_end(struct g5_shell_session *s) {
 if(!s)return;
 if(s->context.active)++s->revocations;
 g5_session_context_revoke(&s->context);
}
