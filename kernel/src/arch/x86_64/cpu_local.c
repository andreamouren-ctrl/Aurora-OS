#include <stddef.h>
#include <stdint.h>

#include <aurora/apic.h>
#include <aurora/cpu_local.h>

#define IA32_GS_BASE        0xC0000101u
#define IA32_KERNEL_GS_BASE 0xC0000102u

static struct aurora_cpu_local cpu_locals[AURORA_MAX_SMP_CPUS];
static struct aurora_cpu_local *bootstrap_local;
static bool apic_lookup_enabled;

_Static_assert(offsetof(struct aurora_cpu_local, self) == AURORA_CPU_LOCAL_SELF_OFFSET,
               "CPU-local self offset drifted");
_Static_assert(offsetof(struct aurora_cpu_local, logical_id) == AURORA_CPU_LOCAL_LOGICAL_ID_OFFSET,
               "CPU-local logical-id offset drifted");
_Static_assert(offsetof(struct aurora_cpu_local, current_space) == AURORA_CPU_LOCAL_CURRENT_SPACE_OFFSET,
               "CPU-local current-space offset drifted");
_Static_assert(offsetof(struct aurora_cpu_local, syscall_kernel_rsp) == AURORA_CPU_LOCAL_SYSCALL_RSP_OFFSET,
               "CPU-local syscall RSP offset drifted");
_Static_assert(offsetof(struct aurora_cpu_local, syscall_user_rsp_scratch) == AURORA_CPU_LOCAL_USER_RSP_OFFSET,
               "CPU-local user RSP offset drifted");

static void wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile (
        "wrmsr"
        :
        : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32))
    );
}

static void zero_local(struct aurora_cpu_local *local) {
    uint8_t *bytes = (uint8_t *)local;
    for (size_t i = 0u; i < sizeof(*local); ++i) bytes[i] = 0u;
    local->scheduler_current_index = UINT32_MAX;
    local->scheduler_idle_index = UINT32_MAX;
}

static bool local_configured(const struct aurora_cpu_local *local) {
    return local != NULL && local->self == local;
}

bool cpu_local_activate_current(uint32_t logical_id) {
    if (logical_id >= AURORA_MAX_SMP_CPUS ||
        !local_configured(&cpu_locals[logical_id])) return false;

    wrmsr(IA32_GS_BASE, 0u);
    wrmsr(IA32_KERNEL_GS_BASE, (uint64_t)(uintptr_t)&cpu_locals[logical_id]);
    return true;
}

bool cpu_local_init_bootstrap(uint32_t logical_id, uint32_t lapic_id_value) {
    if (logical_id >= AURORA_MAX_SMP_CPUS) return false;

    struct aurora_cpu_local *local = &cpu_locals[logical_id];
    zero_local(local);
    local->self = local;
    local->logical_id = logical_id;
    local->lapic_id = lapic_id_value;
    bootstrap_local = local;
    apic_lookup_enabled = false;
    return cpu_local_activate_current(logical_id);
}

bool cpu_local_register(uint32_t logical_id, uint32_t lapic_id_value, bool bootstrap) {
    if (logical_id >= AURORA_MAX_SMP_CPUS) return false;

    struct aurora_cpu_local *local = &cpu_locals[logical_id];
    if (!local_configured(local)) {
        zero_local(local);
        local->self = local;
    }
    local->logical_id = logical_id;
    local->lapic_id = lapic_id_value;

    if (bootstrap) bootstrap_local = local;
    return true;
}

void cpu_local_enable_apic_lookup(void) {
    apic_lookup_enabled = true;
}

struct aurora_cpu_local *cpu_local_at(uint32_t logical_id) {
    if (logical_id >= AURORA_MAX_SMP_CPUS ||
        !local_configured(&cpu_locals[logical_id])) return NULL;
    return &cpu_locals[logical_id];
}

struct aurora_cpu_local *cpu_local_current(void) {
    if (!apic_lookup_enabled) return bootstrap_local;

    uint32_t current_lapic = lapic_id();
    for (uint32_t i = 0u; i < AURORA_MAX_SMP_CPUS; ++i) {
        struct aurora_cpu_local *local = &cpu_locals[i];
        if (local_configured(local) && local->lapic_id == current_lapic) return local;
    }
    return NULL;
}

struct vmm_address_space *cpu_local_current_space(void) {
    struct aurora_cpu_local *local = cpu_local_current();
    return local != NULL ? local->current_space : NULL;
}

void cpu_local_set_current_space(struct vmm_address_space *space) {
    struct aurora_cpu_local *local = cpu_local_current();
    if (local != NULL) local->current_space = space;
}

void cpu_local_set_syscall_kernel_rsp(uint64_t stack_top) {
    struct aurora_cpu_local *local = cpu_local_current();
    if (local != NULL) local->syscall_kernel_rsp = stack_top;
}
