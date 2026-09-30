#include <aurora/arch.h>
#include <aurora/log.h>
#include <aurora/panic.h>

void kernel_panic(const char *reason) {
    log_line("");
    log_line("========================================");
    log_line("AURORA KERNEL PANIC");

    if (reason != 0) {
        log_write("Reason: ");
        log_line(reason);
    }

    log_line("System halted.");
    log_line("========================================");

    arch_halt();
}
