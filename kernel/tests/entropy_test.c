#include <aurora/entropy.h>
#include <aurora/entropy_source.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FAKE_SAMPLE_CAPACITY 32u
#define NO_FAILURE ((size_t)-1)

struct fake_source {
    struct aurora_entropy_source_caps caps;
    uint64_t samples[FAKE_SAMPLE_CAPACITY];
    size_t sample_count;
    size_t sample_index;
    size_t fail_at;
    size_t fail_once_at;
    bool fail_once_consumed;
};

static int failures;

static void expect(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

static bool fake_probe(
    void *context,
    struct aurora_entropy_source_caps *out_caps
) {
    struct fake_source *source = (struct fake_source *)context;

    if (source == NULL || out_caps == NULL) {
        return false;
    }

    *out_caps = source->caps;
    return true;
}

static bool fake_read_seed64(void *context, uint64_t *out_value) {
    struct fake_source *source = (struct fake_source *)context;

    if (source == NULL || out_value == NULL) {
        return false;
    }

    if (source->sample_index == source->fail_once_at &&
        !source->fail_once_consumed) {
        source->fail_once_consumed = true;
        return false;
    }

    if (source->sample_index == source->fail_at) {
        return false;
    }

    if (source->sample_index >= source->sample_count) {
        return false;
    }

    *out_value = source->samples[source->sample_index++];
    return true;
}

static bool fake_read_aux64(void *context, uint64_t *out_value) {
    (void)context;

    if (out_value == NULL) {
        return false;
    }

    *out_value = UINT64_C(0xA5A5A5A5A5A5A5A5);
    return true;
}

bool arch_entropy_source_ops(struct aurora_entropy_source_ops *out_ops) {
    (void)out_ops;
    return false;
}

static struct aurora_entropy_source_ops fake_ops(struct fake_source *source) {
    struct aurora_entropy_source_ops ops;
    ops.context = source;
    ops.probe = fake_probe;
    ops.read_seed64 = fake_read_seed64;
    ops.read_aux64 = fake_read_aux64;
    return ops;
}

static void prepare_good_source(struct fake_source *source) {
    memset(source, 0, sizeof(*source));
    source->caps.trusted_seed = true;
    source->caps.auxiliary_random = true;
    source->caps.source_flags =
        AURORA_ENTROPY_SOURCE_RDSEED |
        AURORA_ENTROPY_SOURCE_RDRAND;
    source->fail_at = NO_FAILURE;
    source->fail_once_at = NO_FAILURE;
    source->sample_count = FAKE_SAMPLE_CAPACITY;

    for (size_t i = 0u; i < source->sample_count; ++i) {
        source->samples[i] = UINT64_C(0x1020304050607000) + (uint64_t)i + 1u;
    }
}

static bool all_zero(const uint8_t *buffer, size_t size) {
    for (size_t i = 0u; i < size; ++i) {
        if (buffer[i] != 0u) {
            return false;
        }
    }

    return true;
}

static void test_untrusted_source_stays_unready(void) {
    struct fake_source source;
    prepare_good_source(&source);
    source.caps.trusted_seed = false;
    source.caps.source_flags = AURORA_ENTROPY_SOURCE_RDRAND;

    struct aurora_entropy_source_ops ops = fake_ops(&source);
    expect(!entropy_init_with_source(&ops), "RDRAND-only source must not become trusted seed");
    expect(!entropy_ready(), "RDRAND-only source must remain unready");

    struct aurora_entropy_status status = entropy_get_status();
    expect(status.initialized, "untrusted source should still mark service initialized");
    expect(status.source_flags == AURORA_ENTROPY_SOURCE_RDRAND,
           "untrusted source flags should be observable");
}

static void test_good_source_initializes_and_fills(void) {
    struct fake_source source;
    prepare_good_source(&source);
    struct aurora_entropy_source_ops ops = fake_ops(&source);

    expect(entropy_init_with_source(&ops), "healthy RDSEED source should initialize");
    expect(entropy_ready(), "healthy source should report ready");

    struct aurora_entropy_status status = entropy_get_status();
    expect(status.startup_samples == AURORA_ENTROPY_STARTUP_SAMPLES,
           "startup health test sample count should be recorded");

    uint8_t output[16];
    memset(output, 0, sizeof(output));
    expect(entropy_fill_seed(output, sizeof(output)), "healthy source should fill seed bytes");

    uint64_t first = source.samples[AURORA_ENTROPY_STARTUP_SAMPLES];
    uint64_t second = source.samples[AURORA_ENTROPY_STARTUP_SAMPLES + 1u];

    for (size_t i = 0u; i < 8u; ++i) {
        expect(output[i] == (uint8_t)(first >> (i * 8u)),
               "first seed word should use explicit little-endian byte extraction");
        expect(output[8u + i] == (uint8_t)(second >> (i * 8u)),
               "second seed word should use explicit little-endian byte extraction");
    }

    status = entropy_get_status();
    expect(status.output_words == 2u, "successful fill should account output words");
}

static void test_startup_duplicate_fails_health(void) {
    struct fake_source source;
    prepare_good_source(&source);
    source.samples[1] = source.samples[0];
    struct aurora_entropy_source_ops ops = fake_ops(&source);

    expect(!entropy_init_with_source(&ops), "duplicate startup sample must fail initialization");

    struct aurora_entropy_status status = entropy_get_status();
    expect(!status.ready, "health failure must leave source unready");
    expect(status.health_failed, "health failure must latch degraded state");
    expect(status.health_failures == 1u, "health failure counter should increment");
    expect(status.startup_samples == 1u, "only accepted startup samples should be counted");
}

static void test_runtime_duplicate_disables_source_and_zeroes_output(void) {
    struct fake_source source;
    prepare_good_source(&source);
    source.samples[AURORA_ENTROPY_STARTUP_SAMPLES + 1u] =
        source.samples[AURORA_ENTROPY_STARTUP_SAMPLES];
    struct aurora_entropy_source_ops ops = fake_ops(&source);

    expect(entropy_init_with_source(&ops), "runtime duplicate test needs initialized source");

    uint8_t output[16];
    memset(output, 0xCC, sizeof(output));
    expect(!entropy_fill_seed(output, sizeof(output)),
           "continuous-test duplicate must fail seed request");
    expect(all_zero(output, sizeof(output)),
           "failed seed request must clear the complete output buffer");

    struct aurora_entropy_status status = entropy_get_status();
    expect(!status.ready, "runtime health failure must disable trusted output");
    expect(status.health_failed, "runtime health failure must latch");
    expect(status.health_failures == 1u, "runtime health failure should be counted");
}

static void test_transient_source_failure_fails_request_but_not_health(void) {
    struct fake_source source;
    prepare_good_source(&source);
    source.fail_at = AURORA_ENTROPY_STARTUP_SAMPLES;
    struct aurora_entropy_source_ops ops = fake_ops(&source);

    expect(entropy_init_with_source(&ops), "transient failure test needs initialized source");

    uint8_t output[8];
    memset(output, 0xCC, sizeof(output));
    expect(!entropy_fill_seed(output, sizeof(output)),
           "source read failure must fail request");
    expect(all_zero(output, sizeof(output)),
           "source read failure must clear caller output");

    struct aurora_entropy_status status = entropy_get_status();
    expect(status.ready, "transient source read failure must not masquerade as health failure");
    expect(!status.health_failed, "transient source read failure must not latch health failure");
    expect(status.source_failures == 1u, "source read failure should be counted");
}

static void test_invalid_requests_fail_closed(void) {
    struct fake_source source;
    prepare_good_source(&source);
    struct aurora_entropy_source_ops ops = fake_ops(&source);
    expect(entropy_init_with_source(&ops), "invalid request test needs initialized source");

    uint8_t one = 0xCCu;
    expect(!entropy_fill_seed(&one, 0u), "zero-length request must be rejected");
    expect(!entropy_fill_seed(NULL, 1u), "NULL output must be rejected");
}

static void test_startup_transient_rdseed_refill(void) {
    struct fake_source source;
    prepare_good_source(&source);
    source.fail_once_at = 7u; /* simulate the QEMU eighth RDSEED refill */
    struct aurora_entropy_source_ops ops = fake_ops(&source);
    expect(entropy_init_with_source(&ops),
           "one temporary RDSEED miss must be retried during startup");
    struct aurora_entropy_status status = entropy_get_status();
    expect(status.ready && status.startup_samples == AURORA_ENTROPY_STARTUP_SAMPLES,
           "all eight valid samples must still pass startup health");
    expect(status.source_failures == 1u && !status.health_failed,
           "temporary RDSEED miss must remain observable");
}

static void test_startup_persistent_rdseed_unavailable(void) {
    struct fake_source source;
    prepare_good_source(&source);
    source.fail_at = 7u;
    struct aurora_entropy_source_ops ops = fake_ops(&source);
    expect(!entropy_init_with_source(&ops),
           "persistent RDSEED refill exhaustion must fail closed");
    struct aurora_entropy_status status = entropy_get_status();
    expect(!status.ready && status.startup_samples == 7u &&
           status.source_failures == 8u && !status.health_failed,
           "exhausted retries must not forge a healthy sample");
}

int main(void) {
    test_untrusted_source_stays_unready();
    test_startup_transient_rdseed_refill();
    test_startup_persistent_rdseed_unavailable();
    test_good_source_initializes_and_fills();
    test_startup_duplicate_fails_health();
    test_runtime_duplicate_disables_source_and_zeroes_output();
    test_transient_source_failure_fails_request_but_not_health();
    test_invalid_requests_fail_closed();

    if (failures != 0) {
        fprintf(stderr, "Aurora entropy policy tests: %d failure(s)\n", failures);
        return 1;
    }

    puts("Aurora entropy policy tests: PASS");
    return 0;
}
