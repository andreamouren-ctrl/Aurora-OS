#include <assert.h>
#include <stdint.h>
#include <aurora/g5_surface_configure.h>

int main(void) {
    struct g5_session_context session={0};
    struct g5_surface_configure surface={0};
    uint64_t serial=123;
    assert(g5_session_context_begin(&session,11));
    assert(!g5_surface_configure_bind(&surface,&session,0));
    assert(g5_surface_configure_bind(&surface,&session,3));
    assert(!g5_surface_configure_bind(&surface,&session,3));
    assert(!g5_surface_configure_issue(&surface,&session,0,100,&serial) && serial==0);
    assert(!g5_surface_configure_issue(&surface,&session,100,8193,&serial));
    assert(g5_surface_configure_issue(&surface,&session,400,300,&serial) && serial==1);
    assert(!g5_surface_configure_ready(&surface,&session));
    assert(!g5_surface_configure_ack(&surface,&session,1,4));
    assert(g5_surface_configure_issue(&surface,&session,500,400,&serial) && serial==2);
    assert(!g5_surface_configure_ack(&surface,&session,1,3));
    assert(g5_surface_configure_ack(&surface,&session,2,3));
    assert(surface.accepted_width==500 && surface.accepted_height==400);
    assert(g5_surface_configure_ready(&surface,&session));
    assert(!g5_surface_configure_ack(&surface,&session,2,3));
    uint64_t revised=0;
    assert(g5_session_context_advance(&session,11,1,&revised));
    assert(!g5_surface_configure_ready(&surface,&session));
    assert(!g5_surface_configure_issue(&surface,&session,1,1,&serial));
    g5_surface_configure_revoke(&surface);
    assert(g5_surface_configure_bind(&surface,&session,4));
    assert(g5_surface_configure_issue(&surface,&session,640,480,&serial));
    g5_session_context_revoke(&session);
    assert(!g5_surface_configure_ack(&surface,&session,serial,4));
    assert(!g5_surface_configure_ready(&surface,&session));
    assert(!g5_session_context_begin(&session,11));
    assert(g5_session_context_begin(&session,12));
    assert(!g5_surface_configure_issue(&surface,&session,32,32,&serial));
    g5_surface_configure_revoke(&surface);
    assert(g5_surface_configure_bind(&surface,&session,1));
    surface.next_serial=UINT64_MAX;
    assert(!g5_surface_configure_issue(&surface,&session,32,32,&serial));
    return 0;
}
