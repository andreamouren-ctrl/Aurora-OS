#include <stddef.h>
#include <stdint.h>

#include <aurora/cpu_topology.h>

static void cpuid(
    uint32_t leaf,
    uint32_t subleaf,
    uint32_t *eax,
    uint32_t *ebx,
    uint32_t *ecx,
    uint32_t *edx
) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    __asm__ volatile (
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(subleaf)
    );

    if (eax != NULL) *eax = a;
    if (ebx != NULL) *ebx = b;
    if (ecx != NULL) *ecx = c;
    if (edx != NULL) *edx = d;
}

static void clear_topology(struct aurora_cpu_topology *out) {
    uint8_t *bytes = (uint8_t *)out;
    for (size_t i = 0u; i < sizeof(*out); ++i) bytes[i] = 0u;
}

static uint32_t low_mask(uint32_t bits) {
    if (bits == 0u) return 0u;
    if (bits >= 32u) return UINT32_MAX;
    return (1u << bits) - 1u;
}

static uint32_t ceil_log2_u32(uint32_t value) {
    if (value <= 1u) return 0u;
    uint32_t bits = 0u;
    --value;
    while (value != 0u) {
        value >>= 1u;
        ++bits;
    }
    return bits;
}

static bool detect_x2apic_leaf(
    uint32_t leaf,
    struct aurora_cpu_topology *out
) {
    uint32_t smt_shift = 0u;
    uint32_t core_shift = 0u;
    uint32_t threads_per_core = 1u;
    uint32_t logical_per_package = 1u;
    uint32_t x2apic_id = 0u;
    bool saw_level = false;
    bool saw_core = false;

    for (uint32_t subleaf = 0u; subleaf < 32u; ++subleaf) {
        uint32_t eax = 0u;
        uint32_t ebx = 0u;
        uint32_t ecx = 0u;
        uint32_t edx = 0u;
        cpuid(leaf, subleaf, &eax, &ebx, &ecx, &edx);

        uint32_t logical_count = ebx & 0xFFFFu;
        uint32_t level_type = (ecx >> 8u) & 0xFFu;
        if (logical_count == 0u || level_type == 0u) break;

        saw_level = true;
        x2apic_id = edx;
        uint32_t shift = eax & 0x1Fu;

        if (level_type == 1u) {
            smt_shift = shift;
            threads_per_core = logical_count;
        } else if (level_type == 2u) {
            core_shift = shift;
            logical_per_package = logical_count;
            saw_core = true;
        }
    }

    if (!saw_level) return false;

    if (!saw_core) {
        core_shift = smt_shift;
        logical_per_package = threads_per_core;
    }

    if (core_shift < smt_shift || threads_per_core == 0u) return false;

    uint32_t core_bits = core_shift - smt_shift;
    uint32_t cores_per_package = logical_per_package / threads_per_core;
    if (cores_per_package == 0u) cores_per_package = 1u;

    out->valid = true;
    out->x2apic_id = x2apic_id;
    out->thread_id = x2apic_id & low_mask(smt_shift);
    out->core_id = (x2apic_id >> smt_shift) & low_mask(core_bits);
    out->package_id = core_shift < 32u ? (x2apic_id >> core_shift) : 0u;
    out->threads_per_core = threads_per_core;
    out->cores_per_package = cores_per_package;
    return true;
}

static bool vendor_is_amd(void) {
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;
    cpuid(0u, 0u, NULL, &ebx, &ecx, &edx);

    /* "AuthenticAMD" = EBX, EDX, ECX. */
    return ebx == 0x68747541u &&
        edx == 0x69746E65u &&
        ecx == 0x444D4163u;
}

static bool detect_amd_extended(
    struct aurora_cpu_topology *out
) {
    uint32_t max_extended = 0u;
    cpuid(0x80000000u, 0u, &max_extended, NULL, NULL, NULL);
    if (max_extended < 0x8000001Eu) return false;

    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    cpuid(0x8000001Eu, 0u, &eax, &ebx, NULL, NULL);

    uint32_t threads_per_core = ((ebx >> 8u) & 0xFFu) + 1u;
    uint32_t core_id = ebx & 0xFFu;
    uint32_t cores_per_package = 1u;

    if (max_extended >= 0x80000008u) {
        uint32_t ecx = 0u;
        cpuid(0x80000008u, 0u, NULL, NULL, &ecx, NULL);
        cores_per_package = (ecx & 0xFFu) + 1u;
    }

    uint64_t logical_wide =
        (uint64_t)cores_per_package * (uint64_t)threads_per_core;
    if (logical_wide == 0u || logical_wide > UINT32_MAX) return false;

    uint32_t logical_per_package = (uint32_t)logical_wide;
    uint32_t package_shift = ceil_log2_u32(logical_per_package);
    uint32_t thread_bits = ceil_log2_u32(threads_per_core);

    out->valid = true;
    out->x2apic_id = eax;
    out->thread_id = eax & low_mask(thread_bits);
    out->core_id = core_id;
    out->package_id = package_shift < 32u ? (eax >> package_shift) : 0u;
    out->threads_per_core = threads_per_core;
    out->cores_per_package = cores_per_package;
    return true;
}

static bool detect_legacy(
    uint32_t max_basic,
    struct aurora_cpu_topology *out
) {
    uint32_t ebx = 0u;
    cpuid(1u, 0u, NULL, &ebx, NULL, NULL);

    uint32_t apic_id = (ebx >> 24u) & 0xFFu;
    uint32_t logical_per_package = (ebx >> 16u) & 0xFFu;
    if (logical_per_package == 0u) logical_per_package = 1u;

    uint32_t cores_per_package = 1u;
    if (max_basic >= 4u) {
        uint32_t eax = 0u;
        cpuid(4u, 0u, &eax, NULL, NULL, NULL);
        if ((eax & 0x1Fu) != 0u)
            cores_per_package = ((eax >> 26u) & 0x3Fu) + 1u;
    }

    uint32_t threads_per_core = logical_per_package / cores_per_package;
    if (threads_per_core == 0u) threads_per_core = 1u;

    uint32_t thread_bits = ceil_log2_u32(threads_per_core);
    uint32_t core_bits = ceil_log2_u32(cores_per_package);
    uint32_t package_shift = thread_bits + core_bits;

    out->valid = true;
    out->x2apic_id = apic_id;
    out->thread_id = apic_id & low_mask(thread_bits);
    out->core_id = thread_bits < 32u
        ? ((apic_id >> thread_bits) & low_mask(core_bits))
        : 0u;
    out->package_id = package_shift < 32u
        ? (apic_id >> package_shift)
        : 0u;
    out->threads_per_core = threads_per_core;
    out->cores_per_package = cores_per_package;
    return true;
}

bool cpu_topology_detect_current(
    struct aurora_cpu_topology *out
) {
    if (out == NULL) return false;
    clear_topology(out);

    uint32_t max_basic = 0u;
    cpuid(0u, 0u, &max_basic, NULL, NULL, NULL);

    if (max_basic >= 0x1Fu &&
        detect_x2apic_leaf(0x1Fu, out))
        return true;

    if (max_basic >= 0x0Bu &&
        detect_x2apic_leaf(0x0Bu, out))
        return true;

    if (vendor_is_amd() && detect_amd_extended(out))
        return true;

    if (max_basic >= 1u)
        return detect_legacy(max_basic, out);

    return false;
}
