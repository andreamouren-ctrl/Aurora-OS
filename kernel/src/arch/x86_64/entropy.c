#include <aurora/entropy.h>
#include <aurora/entropy_source.h>

#include <stdbool.h>
#include <stdint.h>

#define X86_ENTROPY_INSTRUCTION_RETRIES 16u

static bool rdseed_supported;
static bool rdrand_supported;

static void cpuid(
    uint32_t leaf,
    uint32_t subleaf,
    uint32_t *eax,
    uint32_t *ebx,
    uint32_t *ecx,
    uint32_t *edx
) {
    uint32_t a = leaf;
    uint32_t b = 0u;
    uint32_t c = subleaf;
    uint32_t d = 0u;

    __asm__ volatile(
        "cpuid"
        : "+a"(a), "=b"(b), "+c"(c), "=d"(d)
        :
        : "memory"
    );

    if (eax != 0) *eax = a;
    if (ebx != 0) *ebx = b;
    if (ecx != 0) *ecx = c;
    if (edx != 0) *edx = d;
}

static uint32_t maximum_basic_cpuid_leaf(void) {
    uint32_t eax = 0u;
    cpuid(0u, 0u, &eax, 0, 0, 0);
    return eax;
}

static bool try_rdseed64(uint64_t *out_value) {
    if (out_value == 0 || !rdseed_supported) {
        return false;
    }

    for (uint32_t attempt = 0u;
         attempt < X86_ENTROPY_INSTRUCTION_RETRIES;
         ++attempt) {
        uint64_t value = 0u;
        uint8_t success = 0u;

        __asm__ volatile(
            "rdseed %0; setc %1"
            : "=r"(value), "=qm"(success)
            :
            : "cc"
        );

        if (success != 0u) {
            *out_value = value;
            return true;
        }

        __asm__ volatile("pause");
    }

    return false;
}

static bool try_rdrand64(uint64_t *out_value) {
    if (out_value == 0 || !rdrand_supported) {
        return false;
    }

    for (uint32_t attempt = 0u;
         attempt < X86_ENTROPY_INSTRUCTION_RETRIES;
         ++attempt) {
        uint64_t value = 0u;
        uint8_t success = 0u;

        __asm__ volatile(
            "rdrand %0; setc %1"
            : "=r"(value), "=qm"(success)
            :
            : "cc"
        );

        if (success != 0u) {
            *out_value = value;
            return true;
        }

        __asm__ volatile("pause");
    }

    return false;
}

static bool x86_entropy_probe(
    void *context,
    struct aurora_entropy_source_caps *out_caps
) {
    (void)context;

    if (out_caps == 0) {
        return false;
    }

    rdseed_supported = false;
    rdrand_supported = false;

    uint32_t maximum_leaf = maximum_basic_cpuid_leaf();

    if (maximum_leaf >= 1u) {
        uint32_t ecx = 0u;
        cpuid(1u, 0u, 0, 0, &ecx, 0);
        rdrand_supported = (ecx & (1u << 30)) != 0u;
    }

    if (maximum_leaf >= 7u) {
        uint32_t ebx = 0u;
        cpuid(7u, 0u, 0, &ebx, 0, 0);
        rdseed_supported = (ebx & (1u << 18)) != 0u;
    }

    out_caps->trusted_seed = rdseed_supported;
    out_caps->auxiliary_random = rdrand_supported;
    out_caps->source_flags = 0u;

    if (rdseed_supported) {
        out_caps->source_flags |= AURORA_ENTROPY_SOURCE_RDSEED;
    }

    if (rdrand_supported) {
        out_caps->source_flags |= AURORA_ENTROPY_SOURCE_RDRAND;
    }

    return true;
}

static bool x86_entropy_read_seed64(
    void *context,
    uint64_t *out_value
) {
    (void)context;
    return try_rdseed64(out_value);
}

static bool x86_entropy_read_aux64(
    void *context,
    uint64_t *out_value
) {
    (void)context;
    return try_rdrand64(out_value);
}

bool arch_entropy_source_ops(
    struct aurora_entropy_source_ops *out_ops
) {
    if (out_ops == 0) {
        return false;
    }

    out_ops->context = 0;
    out_ops->probe = x86_entropy_probe;
    out_ops->read_seed64 = x86_entropy_read_seed64;
    out_ops->read_aux64 = x86_entropy_read_aux64;
    return true;
}
