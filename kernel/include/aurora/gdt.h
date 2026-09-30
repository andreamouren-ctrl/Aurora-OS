#ifndef AURORA_GDT_H
#define AURORA_GDT_H

#include <stdbool.h>
#include <stdint.h>

#define AURORA_GDT_MAX_CPUS 256u

#define AURORA_KERNEL_CODE_SELECTOR 0x08u
#define AURORA_KERNEL_DATA_SELECTOR 0x10u
#define AURORA_USER_DATA_SELECTOR   0x1Bu
#define AURORA_USER_CODE_SELECTOR   0x23u
#define AURORA_TSS_SELECTOR         0x28u

bool gdt_init_bsp(uint32_t cpu_slot);
bool gdt_init_ap(uint32_t cpu_slot);

void gdt_set_bsp_kernel_stack(
    uint64_t stack_top
);

uint16_t gdt_user_code_selector(void);
uint16_t gdt_user_data_selector(void);

#endif
