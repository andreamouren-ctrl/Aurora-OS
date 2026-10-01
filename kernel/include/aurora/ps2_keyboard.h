#ifndef AURORA_PS2_KEYBOARD_H
#define AURORA_PS2_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

bool ps2_keyboard_init(
    uint32_t destination_apic_id
);

#endif
