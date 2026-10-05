#ifndef AURORA_SMP_H
#define AURORA_SMP_H

#include <stdbool.h>
#include <stdint.h>

#define AURORA_MAX_SMP_CPUS 256u

enum aurora_cpu_state {
    AURORA_CPU_OFFLINE = 0,
    AURORA_CPU_STARTING,
    AURORA_CPU_ONLINE,
    AURORA_CPU_FAILED
};

struct aurora_cpu_runtime {
    uint32_t logical_id;
    uint32_t processor_id;
    uint32_t lapic_id;

    bool bootstrap;

    volatile enum aurora_cpu_state state;
};

bool smp_init(void);

void smp_release_scheduler_aps(void);
uint32_t smp_scheduler_owned_cpu_count(void);

uint32_t smp_cpu_count(void);
uint32_t smp_online_cpu_count(void);

const struct aurora_cpu_runtime *smp_cpu_at(
    uint32_t index
);

#endif
