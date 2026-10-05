#ifndef AURORA_CPU_LOCAL_H
#define AURORA_CPU_LOCAL_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/cpu_local_offsets.h>
#include <aurora/smp.h>

struct vmm_address_space;

struct aurora_cpu_local {
    struct aurora_cpu_local *self;
    uint32_t logical_id;
    uint32_t lapic_id;
    struct vmm_address_space *current_space;
    uint64_t syscall_kernel_rsp;
    uint64_t syscall_user_rsp_scratch;
};

bool cpu_local_init_bootstrap(uint32_t logical_id, uint32_t lapic_id);
bool cpu_local_register(uint32_t logical_id, uint32_t lapic_id, bool bootstrap);
bool cpu_local_activate_current(uint32_t logical_id);
void cpu_local_enable_apic_lookup(void);

struct aurora_cpu_local *cpu_local_current(void);
struct aurora_cpu_local *cpu_local_at(uint32_t logical_id);

struct vmm_address_space *cpu_local_current_space(void);
void cpu_local_set_current_space(struct vmm_address_space *space);

void cpu_local_set_syscall_kernel_rsp(uint64_t stack_top);

#endif
