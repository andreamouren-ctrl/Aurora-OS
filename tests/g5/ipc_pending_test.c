#include <assert.h>
#include <aurora/g5_ipc_pending.h>
int main(void) {
 struct g5_pending_queue q={0};
 g5_pending_reset(&q,21);
 assert(g5_pending_add(&q,20,1)==G5_PENDING_STALE);
 for(uint64_t id=1;id<=G5_IPC_PENDING_LIMIT;++id)
  assert(g5_pending_add(&q,21,id)==G5_PENDING_OK);
 assert(q.count==G5_IPC_PENDING_LIMIT);
 assert(g5_pending_add(&q,21,1)==G5_PENDING_DUPLICATE);
 assert(g5_pending_add(&q,21,17)==G5_PENDING_FULL);
 assert(g5_pending_remove(&q,20,1)==G5_PENDING_STALE);
 assert(g5_pending_remove(&q,21,1)==G5_PENDING_OK);
 assert(g5_pending_remove(&q,21,1)==G5_PENDING_NOT_FOUND);
 assert(g5_pending_add(&q,21,17)==G5_PENDING_OK);
 g5_pending_reset(&q,22);
 assert(q.count==0);
 assert(g5_pending_remove(&q,21,17)==G5_PENDING_STALE);
 assert(g5_pending_add(&q,22,1)==G5_PENDING_OK);
 return 0;
}
