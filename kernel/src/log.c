#include <aurora/arch.h>
#include <aurora/entropy.h>
#include <aurora/log.h>

void log_init(void) {
    arch_serial_init();

    struct aurora_entropy_status entropy = entropy_get_status();

    log_write("[entropy] RDSEED: ");
    log_line(
        (entropy.source_flags & AURORA_ENTROPY_SOURCE_RDSEED) != 0u
            ? "available"
            : "unavailable"
    );

    log_write("[entropy] RDRAND: ");
    log_line(
        (entropy.source_flags & AURORA_ENTROPY_SOURCE_RDRAND) != 0u
            ? "available (auxiliary only)"
            : "unavailable"
    );

    log_write("[entropy] trusted seed service: ");
    log_line(entropy.ready ? "ready" : "unavailable");
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

void log_hex64(uint64_t value) {
    static const char digits[] = "0123456789abcdef";

    log_write("0x");

    for (int shift = 60; shift >= 0; shift -= 4) {
        log_putc(digits[(value >> (unsigned)shift) & 0x0Fu]);
    }
}

void log_u64(uint64_t value) {
    char buffer[21];
    unsigned index = 0;

    if (value == 0) {
        log_putc('0');
        return;
    }

    while (value != 0) {
        buffer[index++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (index != 0) {
        log_putc(buffer[--index]);
    }
}
