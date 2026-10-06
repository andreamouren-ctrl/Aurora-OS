#include "aurora/identity/session_grant_memory.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (0)

static struct aurora_identity_session_grant_record make_record(
    uint8_t tag_seed,
    uint64_t issued_at_ms,
    uint64_t expires_at_ms
) {
    struct aurora_identity_session_grant_record record;
    memset(&record, 0, sizeof(record));
    record.user_id.bytes[0] = 1u;
    for (size_t i = 0u; i < sizeof(record.token_tag); ++i) {
        record.token_tag[i] = (uint8_t)(tag_seed + (uint8_t)i);
    }
    record.issued_at_ms = issued_at_ms;
    record.expires_at_ms = expires_at_ms;
    record.record_version = AURORA_IDENTITY_SESSION_GRANT_RECORD_VERSION;
    return record;
}

static bool test_issue_consume_single_use(void) {
    struct aurora_identity_session_grant_memory_store store;
    struct aurora_identity_session_grant_store_ops ops;
    struct aurora_identity_session_grant_record record = make_record(0x10u, 100u, 200u);
    struct aurora_identity_session_grant_record consumed;

    aurora_identity_session_grant_memory_init(&store);
    ops = aurora_identity_session_grant_memory_ops(&store);

    CHECK(ops.issue_grant(ops.context, &record) ==
        AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_OK);
    CHECK(aurora_identity_session_grant_memory_count(&store) == 1u);
    CHECK(ops.consume_grant(ops.context, record.token_tag, 150u, &consumed) ==
        AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_OK);
    CHECK(memcmp(&consumed, &record, sizeof(record)) == 0);
    CHECK(aurora_identity_session_grant_memory_count(&store) == 0u);
    CHECK(ops.consume_grant(ops.context, record.token_tag, 151u, &consumed) ==
        AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_NOT_FOUND);
    return true;
}

static bool test_conflict_expiry_and_reclaim(void) {
    struct aurora_identity_session_grant_memory_store store;
    struct aurora_identity_session_grant_store_ops ops;
    struct aurora_identity_session_grant_record first = make_record(0x20u, 100u, 120u);
    struct aurora_identity_session_grant_record duplicate = first;
    struct aurora_identity_session_grant_record consumed;

    aurora_identity_session_grant_memory_init(&store);
    ops = aurora_identity_session_grant_memory_ops(&store);

    CHECK(ops.issue_grant(ops.context, &first) ==
        AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_OK);
    CHECK(ops.issue_grant(ops.context, &duplicate) ==
        AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_CONFLICT);
    CHECK(ops.consume_grant(ops.context, first.token_tag, 120u, &consumed) ==
        AURORA_IDENTITY_SESSION_GRANT_STORE_CONSUME_EXPIRED);
    CHECK(aurora_identity_session_grant_memory_count(&store) == 0u);

    CHECK(ops.issue_grant(ops.context, &first) ==
        AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_OK);
    duplicate.issued_at_ms = 130u;
    duplicate.expires_at_ms = 230u;
    CHECK(ops.issue_grant(ops.context, &duplicate) ==
        AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_OK);
    CHECK(aurora_identity_session_grant_memory_count(&store) == 1u);
    return true;
}

static bool test_capacity_and_clear(void) {
    struct aurora_identity_session_grant_memory_store store;
    struct aurora_identity_session_grant_store_ops ops;

    aurora_identity_session_grant_memory_init(&store);
    ops = aurora_identity_session_grant_memory_ops(&store);

    for (size_t i = 0u; i < AURORA_IDENTITY_SESSION_GRANT_MEMORY_CAPACITY; ++i) {
        struct aurora_identity_session_grant_record record =
            make_record((uint8_t)(1u + i), 100u, 1000u);
        CHECK(ops.issue_grant(ops.context, &record) ==
            AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_OK);
    }

    CHECK(aurora_identity_session_grant_memory_count(&store) ==
        AURORA_IDENTITY_SESSION_GRANT_MEMORY_CAPACITY);

    struct aurora_identity_session_grant_record overflow = make_record(0x80u, 100u, 1000u);
    CHECK(ops.issue_grant(ops.context, &overflow) ==
        AURORA_IDENTITY_SESSION_GRANT_STORE_ISSUE_ERROR);

    aurora_identity_session_grant_memory_clear(&store);
    CHECK(aurora_identity_session_grant_memory_count(&store) == 0u);
    return true;
}

int main(void) {
    if (!test_issue_consume_single_use() ||
        !test_conflict_expiry_and_reclaim() ||
        !test_capacity_and_clear()) {
        return 1;
    }

    puts("Aurora Identity transient session-grant store tests: PASS");
    return 0;
}
