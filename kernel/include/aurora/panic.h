#ifndef AURORA_PANIC_H
#define AURORA_PANIC_H

void kernel_panic(const char *reason) __attribute__((noreturn));

#endif
