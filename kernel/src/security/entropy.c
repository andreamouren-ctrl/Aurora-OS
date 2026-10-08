#include <aurora/entropy.h>
#include <aurora/entropy_source.h>

struct entropy_state {
    volatile uint8_t lock;
    bool initialized;
    bool ready;
    bool health_failed;
    bool have_last_sample;
    uint32_t source_flags;
    uint64_t last_sample;
    uint64_t startup_samples;
    uint64_t output_words;
    uint64_t source_failures;
    uint64_t health_failures;
    struct aurora_entropy_source_ops source;
};

static struct entropy_state state;

static void entropy_lock(void) {
    while (__atomic_test_and_set(&state.lock, __ATOMIC_ACQUIRE)) {
    }
}

static void entropy_unlock(void) {
    __atomic_clear(&state.lock, __ATOMIC_RELEASE);
}

static void secure_zero(void *buffer, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;

    if (buffer == 0) {
        return;
    }

    while (size > 0u) {
        *bytes = 0u;
        ++bytes;
        --size;
    }
}

static void reset_state_locked(void) {
    state.initialized = false;
    state.ready = false;
    state.health_failed = false;
    state.have_last_sample = false;
    state.source_flags = 0u;
    state.last_sample = 0u;
    state.startup_samples = 0u;
    state.output_words = 0u;
    state.source_failures = 0u;
    state.health_failures = 0u;
    state.source.context = 0;
    state.source.probe = 0;
    state.source.read_seed64 = 0;
    state.source.read_aux64 = 0;
}

static bool sample_is_healthy(
    uint64_t sample,
    bool have_previous,
    uint64_t previous
) {
    if (sample == 0u || sample == UINT64_MAX) {
        return false;
    }

    if (have_previous && sample == previous) {
        return false;
    }

    return true;
}

static void store_word_bytes(
    uint8_t *destination,
    size_t count,
    uint64_t word
) {
    for (size_t i = 0u; i < count; ++i) {
        destination[i] = (uint8_t)(word >> (i * 8u));
    }
}

bool entropy_init_with_source(
    const struct aurora_entropy_source_ops *ops
) {
    struct aurora_entropy_source_caps caps;
    bool have_previous = false;
    uint64_t previous = 0u;
    uint64_t startup_samples = 0u;
    uint64_t source_failures = 0u;
    uint64_t health_failures = 0u;
    bool health_failed = false;

    caps.trusted_seed = false;
    caps.auxiliary_random = false;
    caps.source_flags = 0u;

    if (ops == 0 || ops->probe == 0 ||
        !ops->probe(ops->context, &caps)) {
        entropy_lock();
        reset_state_locked();
        state.initialized = true;
        entropy_unlock();
        return false;
    }

    if (!caps.trusted_seed || ops->read_seed64 == 0) {
        entropy_lock();
        reset_state_locked();
        state.initialized = true;
        state.source_flags = caps.source_flags;
        state.source = *ops;
        entropy_unlock();
        return false;
    }

    for (uint32_t i = 0u; i < AURORA_ENTROPY_STARTUP_SAMPLES; ++i) {
        uint64_t sample = 0u;

        /* RDSEED is permitted to report temporary unavailability when its
         * hardware entropy pool is empty. Retry each STARTUP sample with a
         * finite budget; never substitute auxiliary RDRAND, and never retry
         * a failed health test. A persistently unavailable source remains
         * fail-closed. */
        bool acquired = false;
        for (uint32_t attempt = 0u; attempt < 8u; ++attempt) {
            if (ops->read_seed64(ops->context, &sample)) {
                acquired = true;
                break;
            }
            ++source_failures;
        }
        if (!acquired) break;

        if (!sample_is_healthy(sample, have_previous, previous)) {
            ++health_failures;
            health_failed = true;
            break;
        }

        previous = sample;
        have_previous = true;
        ++startup_samples;
    }

    entropy_lock();
    reset_state_locked();
    state.initialized = true;
    state.source_flags = caps.source_flags;
    state.source_failures = source_failures;
    state.health_failures = health_failures;
    state.health_failed = health_failed;
    state.startup_samples = startup_samples;
    state.source = *ops;

    if (startup_samples == AURORA_ENTROPY_STARTUP_SAMPLES &&
        !health_failed && have_previous) {
        state.ready = true;
        state.have_last_sample = true;
        state.last_sample = previous;
    }

    bool ready = state.ready;
    entropy_unlock();
    return ready;
}

bool entropy_init(void) {
    struct aurora_entropy_source_ops ops;

    ops.context = 0;
    ops.probe = 0;
    ops.read_seed64 = 0;
    ops.read_aux64 = 0;

    if (!arch_entropy_source_ops(&ops)) {
        entropy_lock();
        reset_state_locked();
        state.initialized = true;
        entropy_unlock();
        return false;
    }

    return entropy_init_with_source(&ops);
}

bool entropy_ready(void) {
    entropy_lock();
    bool ready = state.ready;
    entropy_unlock();
    return ready;
}

bool entropy_fill_seed(void *buffer, size_t size) {
    uint8_t *output = (uint8_t *)buffer;

    if (buffer == 0 || size == 0u ||
        size > AURORA_ENTROPY_MAX_SEED_REQUEST) {
        if (buffer != 0 && size <= AURORA_ENTROPY_MAX_SEED_REQUEST) {
            secure_zero(buffer, size);
        }
        return false;
    }

    entropy_lock();

    if (!state.ready || state.source.read_seed64 == 0) {
        secure_zero(buffer, size);
        entropy_unlock();
        return false;
    }

    size_t offset = 0u;

    while (offset < size) {
        uint64_t sample = 0u;

        if (!state.source.read_seed64(state.source.context, &sample)) {
            ++state.source_failures;
            secure_zero(buffer, size);
            entropy_unlock();
            return false;
        }

        if (!sample_is_healthy(
                sample,
                state.have_last_sample,
                state.last_sample)) {
            ++state.health_failures;
            state.health_failed = true;
            state.ready = false;
            secure_zero(buffer, size);
            entropy_unlock();
            return false;
        }

        state.have_last_sample = true;
        state.last_sample = sample;
        ++state.output_words;

        size_t remaining = size - offset;
        size_t chunk = remaining < sizeof(sample)
            ? remaining
            : sizeof(sample);

        store_word_bytes(output + offset, chunk, sample);
        offset += chunk;
    }

    entropy_unlock();
    return true;
}

struct aurora_entropy_status entropy_get_status(void) {
    struct aurora_entropy_status status;

    entropy_lock();
    status.initialized = state.initialized;
    status.ready = state.ready;
    status.health_failed = state.health_failed;
    status.source_flags = state.source_flags;
    status.startup_samples = state.startup_samples;
    status.output_words = state.output_words;
    status.source_failures = state.source_failures;
    status.health_failures = state.health_failures;
    entropy_unlock();

    return status;
}
