#ifndef AURORA_ENTROPY_H
#define AURORA_ENTROPY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AURORA_ENTROPY_STARTUP_SAMPLES 8u
#define AURORA_ENTROPY_MAX_SEED_REQUEST 1024u

#define AURORA_ENTROPY_SOURCE_RDSEED (1u << 0)
#define AURORA_ENTROPY_SOURCE_RDRAND (1u << 1)

struct aurora_entropy_status {
    bool initialized;
    bool ready;
    bool health_failed;
    uint32_t source_flags;
    uint64_t startup_samples;
    uint64_t output_words;
    uint64_t source_failures;
    uint64_t health_failures;
};

/*
 * Initialize the platform entropy seed service.
 *
 * This service intentionally exposes seed material, not a general-purpose
 * random-number API. Consumers such as Aurora Identity must feed this material
 * into a reviewed DRBG and follow their own reseed policy.
 *
 * A false return means Aurora has no currently qualified trusted seed source.
 * The operating system may continue booting, but security-sensitive consumers
 * must remain fail-closed until entropy_ready() becomes true.
 */
bool entropy_init(void);

bool entropy_ready(void);

/*
 * Fill caller-owned memory with trusted seed material.
 *
 * The request fails closed if the source is unavailable or a continuous health
 * test fails. On failure the entire output buffer is zeroed.
 */
bool entropy_fill_seed(void *buffer, size_t size);

struct aurora_entropy_status entropy_get_status(void);

#endif
