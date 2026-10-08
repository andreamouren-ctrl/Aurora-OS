#include <assert.h>
#include <aurora/g5_presentation_queue.h>
int main(void) {
 struct g5_presentation_queue q={0};
 assert(g5_presentation_queue_begin(&q,9));
 assert(g5_presentation_queue_submit(&q,9,1,40,2));
 assert(!g5_presentation_queue_submit(&q,9,1,40,2));
 assert(!g5_presentation_queue_complete(&q,9,1,40,3));
 assert(g5_presentation_queue_complete(&q,9,1,40,2));
 assert(!g5_presentation_queue_complete(&q,9,1,40,2));
 assert(g5_presentation_queue_submit(&q,9,2,40,3));
 assert(g5_presentation_queue_cancel(&q,9,2));
 assert(!g5_presentation_queue_cancel(&q,9,2));
 for(unsigned i=0;i<G5_PRESENTATION_QUEUE_CAPACITY;i++)
  assert(g5_presentation_queue_submit(&q,9,3+i,50+i,1));
 assert(!g5_presentation_queue_submit(&q,9,99,99,1));
 assert(!g5_presentation_queue_complete(&q,8,3,50,1));
 g5_presentation_queue_revoke(&q);
 assert(!g5_presentation_queue_complete(&q,9,3,50,1));
 assert(!g5_presentation_queue_begin(&q,9));
 assert(g5_presentation_queue_begin(&q,10));
 assert(!g5_presentation_queue_submit(&q,9,100,1,1));
 assert(g5_presentation_queue_submit(&q,10,1,1,1));
 return 0;
}
