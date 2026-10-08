#include <assert.h>
#include <aurora/g5_ipc_dispatch.h>
int main(void) {
 struct g5_dispatch_context d={0};
 d.active_session_generation=17;
 d.last_request_id=9;
 g5_ipc_dispatch_revoke(&d);
 assert(d.active_session_generation==0 && d.last_request_id==0);
 assert(!g5_ipc_dispatch_bind_session(&d,0));
 assert(g5_ipc_dispatch_bind_session(&d,18));
 assert(!g5_ipc_dispatch_bind_session(&d,19));
 g5_ipc_dispatch_revoke(&d);
 return 0;
}
