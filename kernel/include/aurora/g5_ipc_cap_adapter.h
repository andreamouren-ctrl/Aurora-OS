#ifndef AURORA_G5_IPC_CAP_ADAPTER_H
#define AURORA_G5_IPC_CAP_ADAPTER_H
#include <aurora/g5_ipc_abi.h>
#include <aurora/capability.h>
/* Kernel-side receiver adapter. The table MUST belong to the receiving
 * principal. This check does not substitute for session/opcode authorization. */
bool g5_ipc_kernel_cap_check(void *context, aurora_cap_handle handle,
                             enum aurora_cap_type type, uint64_t rights);
#endif
