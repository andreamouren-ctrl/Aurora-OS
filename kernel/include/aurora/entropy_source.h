#ifndef AURORA_ENTROPY_SOURCE_H
#define AURORA_ENTROPY_SOURCE_H

#include <stdbool.h>
#include <stdint.h>

struct aurora_entropy_source_caps {
    bool trusted_seed;
    bool auxiliary_random;
    uint32_t source_flags;
};

struct aurora_entropy_source_ops {
    void *context;

    bool (*probe)(
        void *context,
        struct aurora_entropy_source_caps *out_caps);

    bool (*read_seed64)(
        void *context,
        uint64_t *out_value);

    bool (*read_aux64)(
        void *context,
        uint64_t *out_value);
};

/* Architecture/platform backend. */
bool arch_entropy_source_ops(struct aurora_entropy_source_ops *out_ops);

/*
 * Policy injection point used by host tests and future non-x86 backends.
 * Production boot normally calls entropy_init(), which obtains the platform
 * source from arch_entropy_source_ops().
 */
bool entropy_init_with_source(const struct aurora_entropy_source_ops *ops);

#endif
