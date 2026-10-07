#ifndef AURORA_PS2_MOUSE_H
#define AURORA_PS2_MOUSE_H

#include <stdbool.h>
#include <stdint.h>

bool ps2_mouse_init(uint32_t destination_apic_id);
bool ps2_mouse_selftest(void);

#endif
