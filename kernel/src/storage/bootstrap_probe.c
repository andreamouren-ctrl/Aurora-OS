#include <aurora/block_device.h>
#include <aurora/bootstrap_probe.h>
#include <aurora/log.h>
#include <aurora/panic.h>
#include <aurora/vfs.h>

void bootstrap_storage_probe(void) {
    if (!block_device_self_test()) {
        kernel_panic("Block-device abstraction self-test failed");
    }

    log_line("[storage] block-device abstraction self-test passed");

    if (!vfs_init()) {
        kernel_panic("VFS bootstrap initialization failed");
    }

    if (!vfs_self_test()) {
        kernel_panic("VFS bootstrap self-test failed");
    }

    log_line("[vfs] volatile bootstrap filesystem self-test passed");
}
