#include <assert.h>
#include <aurora/g5_ipc_cap_adapter.h>

/* Controlled host stub mimics the authoritative cap_lookup contract.
 * Real table generation/revocation is covered separately by kernel tests. */
bool cap_lookup(struct aurora_cap_table *table, aurora_cap_handle handle,
                enum aurora_cap_type type, uint64_t rights,
                struct aurora_capability_view *out) {
    if (!table || !out) return false;
    return handle == 42 && type == AURORA_CAP_SURFACE &&
           rights == AURORA_RIGHT_CONTROL;
}
int main(void) {
    struct aurora_cap_table table;
    assert(g5_ipc_kernel_cap_check(&table,42,AURORA_CAP_SURFACE,AURORA_RIGHT_CONTROL));
    assert(!g5_ipc_kernel_cap_check(&table,42,AURORA_CAP_FILE,AURORA_RIGHT_CONTROL));
    assert(!g5_ipc_kernel_cap_check(&table,42,AURORA_CAP_SURFACE,AURORA_RIGHT_WRITE));
    assert(!g5_ipc_kernel_cap_check(NULL,42,AURORA_CAP_SURFACE,AURORA_RIGHT_CONTROL));
    assert(!g5_ipc_kernel_cap_check(&table,0,AURORA_CAP_SURFACE,AURORA_RIGHT_CONTROL));
    assert(!g5_ipc_kernel_cap_check(&table,42,AURORA_CAP_NONE,AURORA_RIGHT_CONTROL));
    return 0;
}
