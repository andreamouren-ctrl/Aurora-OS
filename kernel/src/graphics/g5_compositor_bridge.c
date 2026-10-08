#include <aurora/g5_compositor_bridge.h>
#include <stddef.h>
static bool live(struct g5_compositor_bridge *b) {
 return b&&b->delivery&&b->compositor&&b->compositor->initialized&&
  b->delivery->submission&&b->delivery->bound_generation==b->generation&&
  g5_session_context_authorized(&b->delivery->submission->registry.session,b->generation);
}
bool g5_compositor_bridge_bind(struct g5_compositor_bridge *b,
 struct g5_frame_delivery *d,struct aurora_software_compositor *c) {
 if(!b||!d||!c||b->delivery||!d->submission||!c->initialized||
    !g5_session_context_authorized(&d->submission->registry.session,d->bound_generation))return false;
 b->delivery=d;b->compositor=c;b->generation=d->bound_generation;
 return true;
}
bool g5_compositor_bridge_attach(struct g5_compositor_bridge *b,uint32_t slot,
 int32_t x,int32_t y,int32_t z,uint64_t *node_id) {
 if(node_id)*node_id=0;
 if(!node_id||!live(b)||slot>=G5_SURFACE_REGISTRY_CAPACITY||b->node_ids[slot])return false;
 struct g5_surface_registry_entry *entry=&b->delivery->submission->registry.entries[slot];
 if(!entry->occupied)return false;
 struct g5_surface_bridge *s=&entry->bridge;
 struct aurora_graphics_surface *surface=NULL;
 if(!graphics_surface_lookup(s->owner_caps,s->surface_handle,AURORA_RIGHT_READ,&surface)||
    surface->object_id!=s->object_id||surface->generation!=s->object_generation||
    surface->destroy_requested||!g5_surface_configure_ready(&s->configure,
      &b->delivery->submission->registry.session))return false;
 uint64_t id=0;
 if(!software_compositor_add_surface(b->compositor,surface,x,y,z,255,&id)||!id)return false;
 b->node_ids[slot]=id;*node_id=id;return true;
}
bool g5_compositor_bridge_present(struct g5_compositor_bridge *b,uint32_t slot,
 uint64_t req,uint64_t config,uint64_t *display_serial) {
 if(display_serial)*display_serial=0;
 if(!display_serial||!live(b)||slot>=G5_SURFACE_REGISTRY_CAPACITY||
    !b->node_ids[slot])return false;
 /* Revalidate node binding, current capability and object generation at use. */
 struct g5_surface_registry_entry *entry=&b->delivery->submission->registry.entries[slot];
 if(!entry->occupied)return false;
 struct g5_surface_bridge *bound=&entry->bridge;
 struct aurora_graphics_surface *surface=NULL;
 if(!bound->owner_caps||
    !graphics_surface_lookup(bound->owner_caps,bound->surface_handle,AURORA_RIGHT_READ,&surface)||
    surface->object_id!=bound->object_id||
    surface->generation!=bound->object_generation||
    surface->destroy_requested||
    surface->state==AURORA_GRAPHICS_SURFACE_FREE||
    !g5_surface_configure_ready(&bound->configure,&b->delivery->submission->registry.session))
    return false;
 struct aurora_graphics_surface_snapshot snapshot={0};
 uint64_t receipt=0;
 if(!g5_frame_delivery_publish(b->delivery,slot,req,config,&snapshot,&receipt))return false;
 /* Always release retained committed buffers, even if presentation fails. */
 graphics_surface_snapshot_release(&snapshot);
 if(!software_compositor_compose_present(b->compositor,display_serial)) {
  (void)g5_frame_delivery_abort(b->delivery,b->generation,receipt);
  *display_serial=0;
  return false;
 }
 if(!g5_frame_delivery_ack(b->delivery,b->generation,receipt)) {
  *display_serial=0;
  return false;
 }
 return true;
}
void g5_compositor_bridge_revoke(struct g5_compositor_bridge *b) {
 if(!b)return;
 if(b->compositor)for(unsigned i=0;i<G5_SURFACE_REGISTRY_CAPACITY;i++) {
  if(b->node_ids[i]) {
   (void)software_compositor_remove_surface(b->compositor,b->node_ids[i]);
   b->node_ids[i]=0;
  }
 }
 b->delivery=NULL;b->compositor=NULL;b->generation=0;
}
