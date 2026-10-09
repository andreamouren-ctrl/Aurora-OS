#include "aurora/identity/audit_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c,m) do { if (!(c)) { fprintf(stderr,"FAIL: %s\n",m); exit(1); } } while (0)

static void fill_user(struct aurora_identity_user_id *user, uint8_t seed) {
    memset(user, 0, sizeof(*user));
    for (size_t i = 0u; i < sizeof(user->bytes); ++i) {
        user->bytes[i] = (uint8_t)(seed + (uint8_t)i + 1u);
    }
}

static void test_append_and_read(void) {
    struct aurora_identity_audit_log log;
    struct aurora_identity_user_id user;
    struct aurora_identity_audit_record record;
    aurora_identity_audit_log_init(&log);
    fill_user(&user, 0x20u);

    CHECK(aurora_identity_audit_append(
        &log,
        AURORA_IDENTITY_AUDIT_EVENT_AUTH_SUCCESS,
        AURORA_IDENTITY_AUDIT_OUTCOME_SUCCESS,
        0u,
        UINT64_C(100),
        UINT64_C(7),
        &user), "append auth success");
    CHECK(aurora_identity_audit_count(&log) == 1u, "count after append");
    CHECK(aurora_identity_audit_get_oldest(&log, 0u, &record), "read oldest");
    CHECK(record.sequence == UINT64_C(1), "sequence starts at one");
    CHECK(record.event_type == AURORA_IDENTITY_AUDIT_EVENT_AUTH_SUCCESS, "event type");
    CHECK(record.session_generation == UINT64_C(7), "session generation");
    CHECK(record.has_user_id, "user id present");
    CHECK(memcmp(record.user_id.bytes, user.bytes, sizeof(user.bytes)) == 0, "user id preserved");
}

static void test_anonymous_failure_and_validation(void) {
    struct aurora_identity_audit_log log;
    struct aurora_identity_user_id zero_user;
    aurora_identity_audit_log_init(&log);
    memset(&zero_user, 0, sizeof(zero_user));

    CHECK(aurora_identity_audit_append(
        &log,
        AURORA_IDENTITY_AUDIT_EVENT_AUTH_FAILURE,
        AURORA_IDENTITY_AUDIT_OUTCOME_FAILURE,
        42u,
        UINT64_C(200),
        0u,
        NULL), "anonymous failure event");
    CHECK(!aurora_identity_audit_append(
        &log,
        AURORA_IDENTITY_AUDIT_EVENT_NONE,
        AURORA_IDENTITY_AUDIT_OUTCOME_FAILURE,
        0u, 0u, 0u, NULL), "invalid event rejected");
    CHECK(!aurora_identity_audit_append(
        &log,
        AURORA_IDENTITY_AUDIT_EVENT_AUTH_FAILURE,
        AURORA_IDENTITY_AUDIT_OUTCOME_NONE,
        0u, 0u, 0u, NULL), "invalid outcome rejected");
    CHECK(!aurora_identity_audit_append(
        &log,
        AURORA_IDENTITY_AUDIT_EVENT_AUTH_FAILURE,
        AURORA_IDENTITY_AUDIT_OUTCOME_FAILURE,
        0u, 0u, 0u, &zero_user), "zero explicit user rejected");
}

static void test_ring_wrap_preserves_order(void) {
    struct aurora_identity_audit_log log;
    struct aurora_identity_audit_record record;
    aurora_identity_audit_log_init(&log);

    for (size_t i = 0u; i < AURORA_IDENTITY_AUDIT_CAPACITY + 5u; ++i) {
        CHECK(aurora_identity_audit_append(
            &log,
            AURORA_IDENTITY_AUDIT_EVENT_SESSION_STARTED,
            AURORA_IDENTITY_AUDIT_OUTCOME_SUCCESS,
            (uint32_t)i,
            (uint64_t)i,
            (uint64_t)(i + 1u),
            NULL), "ring append");
    }

    CHECK(aurora_identity_audit_count(&log) == AURORA_IDENTITY_AUDIT_CAPACITY,
        "bounded capacity");
    CHECK(aurora_identity_audit_get_oldest(&log, 0u, &record), "oldest after wrap");
    CHECK(record.sequence == UINT64_C(6), "oldest sequence after wrap");
    CHECK(record.reason_code == 5u, "oldest payload after wrap");

    CHECK(aurora_identity_audit_get_oldest(
        &log, AURORA_IDENTITY_AUDIT_CAPACITY - 1u, &record), "newest after wrap");
    CHECK(record.sequence == (uint64_t)(AURORA_IDENTITY_AUDIT_CAPACITY + 5u),
        "newest sequence after wrap");
}

static void test_clear(void) {
    struct aurora_identity_audit_log log;
    aurora_identity_audit_log_init(&log);
    CHECK(aurora_identity_audit_append(
        &log,
        AURORA_IDENTITY_AUDIT_EVENT_CREDENTIAL_ROTATED,
        AURORA_IDENTITY_AUDIT_OUTCOME_SUCCESS,
        0u, 1u, 1u, NULL), "append before clear");
    aurora_identity_audit_log_clear(&log);
    CHECK(aurora_identity_audit_count(&log) == 0u, "clear count");
    CHECK(log.next_sequence == UINT64_C(1), "clear resets sequence");
}

int main(void) {
    test_append_and_read();
    test_anonymous_failure_and_validation();
    test_ring_wrap_preserves_order();
    test_clear();
    puts("Aurora Identity audit log core tests: PASS");
    return 0;
}
