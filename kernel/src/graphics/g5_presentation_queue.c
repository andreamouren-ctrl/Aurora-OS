#include <aurora/g5_presentation_queue.h>
#include <stddef.h>
#include <stdint.h>
bool g5_presentation_queue_begin(struct g5_presentation_queue *q,uint64_t gen) {
 if(!q||!gen||q->active)return false;
 for(unsigned i=0;i<G5_PRESENTATION_QUEUE_CAPACITY;i++)if(q->entries[i].active)return false;
 if(gen<=q->generation)return false;
 q->generation=gen;q->last_request_id=0;q->active=true;return true;
}
bool g5_presentation_queue_submit(struct g5_presentation_queue *q,uint64_t gen,
 uint64_t req,uint64_t surface,uint64_t config) {
 if(!q||!q->active||gen!=q->generation||!req||req<=q->last_request_id||!surface||!config)return false;
 for(unsigned i=0;i<G5_PRESENTATION_QUEUE_CAPACITY;i++)if(!q->entries[i].active) {
  q->entries[i]=(struct g5_presentation_ticket){gen,req,surface,config,true};
  q->last_request_id=req;return true;
 }
 return false;
}
static struct g5_presentation_ticket *find(struct g5_presentation_queue *q,uint64_t gen,uint64_t req) {
 if(!q||!q->active||gen!=q->generation||!req)return NULL;
 for(unsigned i=0;i<G5_PRESENTATION_QUEUE_CAPACITY;i++)
  if(q->entries[i].active&&q->entries[i].request_id==req)return &q->entries[i];
 return NULL;
}
bool g5_presentation_queue_complete(struct g5_presentation_queue *q,uint64_t gen,
 uint64_t req,uint64_t surface,uint64_t config) {
 struct g5_presentation_ticket *t=find(q,gen,req);
 if(!t||t->surface_id!=surface||t->configure_serial!=config)return false;
 t->active=false;return true;
}
bool g5_presentation_queue_cancel(struct g5_presentation_queue *q,uint64_t gen,uint64_t req) {
 struct g5_presentation_ticket *t=find(q,gen,req);
 if(!t)return false;
 t->active=false;return true;
}
void g5_presentation_queue_revoke(struct g5_presentation_queue *q) {
 if(!q)return;
 for(unsigned i=0;i<G5_PRESENTATION_QUEUE_CAPACITY;i++)q->entries[i].active=false;
 q->active=false;
}
