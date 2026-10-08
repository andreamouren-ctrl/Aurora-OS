#include <aurora/g5_ipc_cap_adapter.h>

/* cap_lookup validates the slot generation, type and required rights against
 * the receiver's actual capability table; not a numeric-handle heuristic. */
bool g5_ipc_kernel_cap_check(void *context, aurora_cap_handle handle,
                             enum aurora_cap_type type, uint64_t rights) {
    if (context == NULL || handle == AURORA_CAP_INVALID ||
        type <= AURORA_CAP_NONE || type >= AURORA_CAP_TYPE_COUNT)
        return false;
    struct aurora_capability_view view = {0};
    return cap_lookup((struct aurora_cap_table *)context, handle,
                      type, rights, &view);
}
