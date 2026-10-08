#include <aurora/g5_frame_delivery.h>
#include <stddef.h>
bool g5_frame_delivery_bind(struct g5_frame_delivery *d,struct g5_frame_submission *f) {
 if(!d||!f||d->submission||!f->registry.session.active||
    !f->queue.active||f->registry.session.generation!=f->queue.generation)return false;
 d->submission=f;d->bound_generation=f->registry.session.generation;
 d->last_delivered_serial=0;return true;
}
bool g5_frame_delivery_publish(struct g5_frame_delivery *d,uint32_t slot,
 uint64_t request_id,uint64_t config,
 struct aurora_graphics_surface_snapshot *snapshot,uint64_t *serial) {
 if(serial)*serial=0;
 if(!d||!d->submission||!serial||!snapshot)return false;
 struct g5_frame_submission *f=d->submission;
 if(!f->registry.session.active||!f->queue.active||
    f->registry.session.generation!=d->bound_generation||
    f->registry.session.generation!=f->queue.generation)return false;
 unsigned free_slot=G5_FRAME_DELIVERY_CAPACITY;
 for(unsigned i=0;i<G5_FRAME_DELIVERY_CAPACITY;i++) {
  if(d->entries[i].active) {
   if(d->entries[i].request_id==request_id)return false;
  } else if(free_slot==G5_FRAME_DELIVERY_CAPACITY)free_slot=i;
 }
 if(free_slot==G5_FRAME_DELIVERY_CAPACITY)return false;
 uint64_t next=0;
 if(!g5_frame_submission_complete(f,slot,request_id,config,snapshot,&next))return false;
 if(next<=d->last_delivered_serial)return false;
 d->entries[free_slot]=(struct g5_frame_delivery_entry){
   f->registry.session.generation,request_id,next,slot,true};
 d->last_delivered_serial=next;*serial=next;return true;
}
bool g5_frame_delivery_ack(struct g5_frame_delivery *d,uint64_t gen,uint64_t serial) {
 if(!d||!d->submission||!gen||!serial||
    !d->submission->registry.session.active||
    !d->submission->queue.active||
    d->submission->queue.generation!=gen||
    d->bound_generation!=gen||
    d->submission->registry.session.generation!=gen)return false;
 for(unsigned i=0;i<G5_FRAME_DELIVERY_CAPACITY;i++) {
  struct g5_frame_delivery_entry *e=&d->entries[i];
  if(e->active&&e->session_generation==gen&&e->presentation_serial==serial) {
   e->active=false;return true;
  }
 }
 return false;
}
void g5_frame_delivery_revoke(struct g5_frame_delivery *d) {
 if(!d)return;
 for(unsigned i=0;i<G5_FRAME_DELIVERY_CAPACITY;i++)d->entries[i].active=false;
 d->submission=NULL;d->bound_generation=0;d->last_delivered_serial=0;
}
