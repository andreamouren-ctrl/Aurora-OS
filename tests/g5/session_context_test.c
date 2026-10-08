#include <assert.h>
#include <stdint.h>
#include <aurora/g5_session_context.h>

int main(void) {
    struct g5_session_context c = {0};
    uint64_t revised=99;
    assert(!g5_session_context_begin(&c,0));
    assert(g5_session_context_begin(&c,7));
    assert(!g5_session_context_begin(&c,8));
    assert(!g5_session_context_authorized(&c,6));
    assert(g5_session_context_authorized(&c,7));
    assert(!g5_session_context_advance(&c,6,1,&revised) && revised==0);
    assert(!g5_session_context_advance(&c,7,2,&revised) && revised==0);
    assert(g5_session_context_advance(&c,7,1,&revised) && revised==2);
    assert(!g5_session_context_advance(&c,7,1,&revised) && revised==0);
    g5_session_context_revoke(&c);
    assert(!g5_session_context_authorized(&c,7));
    assert(!g5_session_context_begin(&c,7));
    assert(g5_session_context_begin(&c,8));
    c.revision=UINT64_MAX;
    assert(!g5_session_context_advance(&c,8,UINT64_MAX,&revised));
    g5_session_context_revoke(&c);
    assert(c.last_revoked_generation==8);
    return 0;
}
