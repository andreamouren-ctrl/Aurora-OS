#include <assert.h>
#include <aurora/g5_shell_session.h>
int main(void) {
 struct g5_shell_session s={0};
 assert(!g5_shell_session_check(&s,2));
 assert(!g5_shell_session_ready(&s,0));
 assert(g5_shell_session_ready(&s,2));
 assert(g5_shell_session_check(&s,2));
 assert(!g5_shell_session_check(&s,3));
 assert(!g5_shell_session_ready(&s,3));
 g5_shell_session_end(&s);
 assert(!g5_shell_session_check(&s,2));
 assert(s.revocations==1);
 g5_shell_session_end(&s);
 assert(s.revocations==1);
 assert(!g5_shell_session_ready(&s,2));
 assert(g5_shell_session_ready(&s,3));
 assert(!g5_shell_session_check(&s,2));
 assert(g5_shell_session_check(&s,3));
 g5_shell_session_end(&s);
 assert(s.revocations==2);
 return 0;
}
