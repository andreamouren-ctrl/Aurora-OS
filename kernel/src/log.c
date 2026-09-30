#include <aurora/arch.h>
#include <aurora/log.h>

void log_init(void) {
    arch_serial_init();
}

void log_putc(char c) {
    if (c == '\n') {
        arch_serial_putc('\r');
    }
    arch_serial_putc(c);
}

void log_write(const char *text) {
    if (text == 0) {
        return;
    }

    while (*text != '\0') {
        log_putc(*text++);
    }
}

void log_line(const char *text) {
    log_write(text);
    log_putc('\n');
}
