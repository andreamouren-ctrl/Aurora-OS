#ifndef AURORA_CPU_TOPOLOGY_H
#define AURORA_CPU_TOPOLOGY_H

#include <stdbool.h>
#include <stdint.h>

struct aurora_cpu_topology {
    bool valid;
    uint32_t x2apic_id;
    uint32_t package_id;
    uint32_t core_id;
    uint32_t thread_id;
    uint32_t threads_per_core;
    uint32_t cores_per_package;
};

/* Detect package/core/SMT identity for the CPU executing this function. */
bool cpu_topology_detect_current(
    struct aurora_cpu_topology *out
);

#endif
