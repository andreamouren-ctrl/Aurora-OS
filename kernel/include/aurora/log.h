#ifndef AURORA_LOG_H
#define AURORA_LOG_H

void log_init(void);
void log_putc(char c);
void log_write(const char *text);
void log_line(const char *text);

#endif
