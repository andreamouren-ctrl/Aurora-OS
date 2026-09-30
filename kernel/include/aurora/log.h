#ifndef AURORA_LOG_H
#define AURORA_LOG_H

#include <stdint.h>

void log_init(void);
void log_putc(char c);
void log_write(const char *text);
void log_line(const char *text);

void log_hex64(uint64_t value);
void log_u64(uint64_t value);

#endif
