#include <aurora/g5_ipc_durable.h>
#include <aurora/capability.h>
#include <stddef.h>
static struct aurora_protected_state_namespace g5_probe_state;
static struct aurora_cap_table g5_probe_caps;

bool g5_ipc_durable_boot_probe(bool *recovered) {
 if(recovered==NULL)return false;
 *recovered=false;
 if(!protected_state_namespace_init(&g5_probe_state,"g5-ipc-probe") ||
    !protected_state_prepare(&g5_probe_state))return false;
 cap_table_init(&g5_probe_caps);
 aurora_cap_handle authority=protected_state_grant(
    &g5_probe_caps,&g5_probe_state,
    AURORA_RIGHT_READ|AURORA_RIGHT_WRITE);
 if(authority==AURORA_CAP_INVALID)return false;
 struct g5_ipc_durable_ledger ledger={
    .capabilities=&g5_probe_caps,.handle=authority,
    .state=&g5_probe_state,.record_name="g5-replay-v1"
 };
 if(!g5_durable_restore(&ledger,UINT64_C(91)))return false;
 *recovered=ledger.last_request_id!=0;
 if(ledger.last_request_id==UINT64_MAX)return false;
 uint64_t next=ledger.last_request_id+1;
 /* Persist admission but intentionally do not execute an effect: a simulated
  * crash between durable reservation and handler execution. Next boot must
  * treat this request ID as spent (at-most-once, not exactly-once). */
 if(!g5_durable_reserve(&ledger,UINT64_C(91),next))return false;
 struct g5_ipc_durable_ledger check={
    .capabilities=&g5_probe_caps,.handle=authority,
    .state=&g5_probe_state,.record_name="g5-replay-v1"
 };
 if(!g5_durable_restore(&check,UINT64_C(91)) ||
    check.last_request_id!=next ||
    g5_durable_reserve(&check,UINT64_C(91),next))return false;
 return cap_revoke(&g5_probe_caps,authority);
}
