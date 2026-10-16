/* Host executable regression test using the production IPC and capability
 * implementations. Compile with:
 * cc -std=c11 -O1 -Wall -Wextra -Werror -Ikernel/include \
 *   kernel/tests/test_ipc_transport_host.c -o /tmp/aurora-ipc-host
 * /tmp/aurora-ipc-host
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "../src/security/capability.c"
#include "../src/ipc/ipc.c"

bool scheduler_wake_thread(aurora_thread_id id) {
    (void)id;
    return true;
}

int main(void) {
    assert(ipc_self_test());
    return 0;
}
