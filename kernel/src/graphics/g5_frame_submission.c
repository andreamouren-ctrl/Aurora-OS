#include <aurora/g5_frame_submission.h>
#include <stddef.h>
bool g5_frame_submission_begin(struct g5_frame_submission *f,uint64_t gen) {
 if(!f||f->registry.session.active||f->queue.active)return false;
 if(!g5_surface_registry_begin(&f->registry,gen))return false;
 if(!g5_presentation_queue_begin(&f->queue,gen)) {
  g5_surface_registry_end(&f->registry);return false;
 }
 return true;
}
bool g5_frame_submission_request(struct g5_frame_submission *f,uint32_t slot,
 uint64_t req,uint64_t *configure_serial) {
 if(configure_serial)*configure_serial=0;
 if(!f||!configure_serial||slot>=G5_SURFACE_REGISTRY_CAPACITY||
    !f->registry.session.active||!f->registry.entries[slot].occupied||
    !g5_surface_configure_ready(&f->registry.entries[slot].bridge.configure,
                               &f->registry.session))return false;
 struct g5_surface_bridge *b=&f->registry.entries[slot].bridge;
 struct aurora_graphics_surface *surface=NULL;
 if(!graphics_surface_lookup(b->owner_caps,b->surface_handle,AURORA_RIGHT_READ,&surface)||
    surface->object_id!=b->object_id||surface->generation!=b->object_generation||
    surface->destroy_requested)return false;
 uint64_t serial=b->configure.accepted_serial;
 if(!g5_presentation_queue_submit(&f->queue,f->registry.session.generation,req,
                                  b->object_id,serial))return false;
 *configure_serial=serial;return true;
}
bool g5_frame_submission_complete(struct g5_frame_submission *f,uint32_t slot,
 uint64_t req,uint64_t config,struct aurora_graphics_surface_snapshot *snapshot,
 uint64_t *presentation_serial) {
 if(presentation_serial)*presentation_serial=0;
 if(!f||!snapshot||!presentation_serial||slot>=G5_SURFACE_REGISTRY_CAPACITY||
    !f->registry.entries[slot].occupied)return false;
 struct g5_surface_bridge *b=&f->registry.entries[slot].bridge;
 if(config!=b->configure.accepted_serial||
    !g5_surface_configure_ready(&b->configure,&f->registry.session))return false;
 struct g5_presentation_ticket *ticket=NULL;
 for(unsigned i=0;i<G5_PRESENTATION_QUEUE_CAPACITY;i++) {
  struct g5_presentation_ticket *t=&f->queue.entries[i];
  if(t->active&&t->request_id==req&&t->session_generation==f->registry.session.generation)
   ticket=t;
 }
 if(!ticket||ticket->surface_id!=b->object_id||ticket->configure_serial!=config)
  return false;
 if(!g5_surface_registry_present(&f->registry,slot,snapshot,presentation_serial))
  return false;
 /* No re-entry between validation, presentation and completion is permitted. */
 if(!g5_presentation_queue_complete(&f->queue,f->registry.session.generation,req,
                                    b->object_id,config))return false;
 return true;
}
bool g5_frame_submission_cancel(struct g5_frame_submission *f,uint64_t req) {
 return f&&g5_presentation_queue_cancel(&f->queue,f->registry.session.generation,req);
}
void g5_frame_submission_end(struct g5_frame_submission *f) {
 if(!f)return;
 g5_presentation_queue_revoke(&f->queue);
 g5_surface_registry_end(&f->registry);
}
