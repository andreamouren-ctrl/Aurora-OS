/* Exercises the production Identity parser with a fault-injected transport.
 * Build: cc -std=c11 -O1 -Wall -Wextra -Werror -ffunction-sections
 * -fdata-sections -Ikernel/include kernel/tests/test_identity_client_ipc.c
 * -Wl,--gc-sections -o /tmp/identity-ipc-test
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../src/service/identity_client.c"

static bool supervisor_step_ok;
static bool reply_available;
static struct aurora_ipc_received injected_reply;

bool service_supervisor_step(struct aurora_service_supervisor *supervisor) {
    (void)supervisor;
    return supervisor_step_ok;
}
bool service_supervisor_receive(
    struct aurora_service_supervisor *supervisor,
    struct aurora_ipc_received *out
) {
    (void)supervisor;
    if (!reply_available) return false;
    *out = injected_reply;
    reply_available = false;
    return true;
}
bool service_supervisor_send(
    struct aurora_service_supervisor *supervisor,
    const void *data,
    uint32_t length
) {
    (void)supervisor;
    (void)data;
    (void)length;
    return true;
}
static void setup(void) {
    supervisor_step_ok = true;
    reply_available = false;
    memset(&injected_reply, 0, sizeof(injected_reply));
    identity_supervisor.state = AURORA_SERVICE_SUPERVISOR_RUNNING;
    client_state = AURORA_IDENTITY_CLIENT_READING_ACTIVITY;
    current_request_id = UINT64_C(123456);
    abandoned_activity_read = true;
    memset(&pending_activity_record, 0, sizeof(pending_activity_record));
}
static void inject_activity(uint64_t request_id, uint32_t state) {
    struct aurora_identity_service_security_activity_result msg = {0};
    msg.header.version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
    msg.header.type = AURORA_IDENTITY_SERVICE_SECURITY_ACTIVITY_RESULT;
    msg.header.request_id = request_id;
    msg.state = state;
    msg.public_error = AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_NONE;
    msg.record_version = 1u;
    msg.sequence = 8u;
    assert(sizeof(msg) <= sizeof(injected_reply.data));
    memcpy(injected_reply.data, &msg, sizeof(msg));
    injected_reply.length = (uint32_t)sizeof(msg);
    reply_available = true;
}
static void test_abandoned_end_discards_immediately(void) {
    setup();
    inject_activity(123456u, AURORA_IDENTITY_SERVICE_SECURITY_ACTIVITY_END);
    identity_client_pump();
    assert(!abandoned_activity_read);
    assert(client_state == AURORA_IDENTITY_CLIENT_READY);
    assert(current_request_id == 0u);
}
static void test_abandoned_record_discards_immediately(void) {
    setup();
    inject_activity(123456u, AURORA_IDENTITY_SERVICE_SECURITY_ACTIVITY_RECORD);
    identity_client_pump();
    assert(!abandoned_activity_read);
    assert(client_state == AURORA_IDENTITY_CLIENT_READY);
    assert(pending_activity_record.sequence == 0u);
}
static void test_mismatched_reply_is_fail_closed(void) {
    setup();
    inject_activity(9999u, AURORA_IDENTITY_SERVICE_SECURITY_ACTIVITY_RECORD);
    identity_client_pump();
    assert(!abandoned_activity_read);
    assert(client_state == AURORA_IDENTITY_CLIENT_ERROR);
    identity_client_pump();
    assert(client_state == AURORA_IDENTITY_CLIENT_ERROR);
}
static void test_unexpected_capability_is_fail_closed(void) {
    setup();
    injected_reply.capability_count = 1u;
    reply_available = true;
    identity_client_pump();
    assert(!abandoned_activity_read);
    assert(client_state == AURORA_IDENTITY_CLIENT_ERROR);
    identity_client_pump();
    assert(client_state == AURORA_IDENTITY_CLIENT_ERROR);
}
static void test_malformed_length_is_fail_closed(void) {
    setup();
    injected_reply.length = 1u;
    reply_available = true;
    identity_client_pump();
    assert(client_state == AURORA_IDENTITY_CLIENT_ERROR);
    assert(!abandoned_activity_read);
}
static void test_supervisor_step_failure_is_fail_closed(void) {
    setup();
    supervisor_step_ok = false;
    identity_client_pump();
    assert(client_state == AURORA_IDENTITY_CLIENT_ERROR);
    assert(!abandoned_activity_read);
    assert(current_request_id == 0u);
}
static void test_supervisor_outage_retires_abandoned_read(void) {
    setup();
    identity_supervisor.state = AURORA_SERVICE_SUPERVISOR_FAILED;
    identity_client_pump();
    assert(client_state == AURORA_IDENTITY_CLIENT_UNAVAILABLE);
    assert(!abandoned_activity_read);
    assert(current_request_id == 0u);
}
static void test_foreign_reauth_result_survives_discard(void) {
    setup();
    client_state = AURORA_IDENTITY_CLIENT_REAUTH_VERIFIED;
    abandoned_activity_read = false;
    assert(!identity_client_discard_completed_security_activity());
    identity_client_pump();
    assert(client_state == AURORA_IDENTITY_CLIENT_REAUTH_VERIFIED);
}
int main(void) {
    test_abandoned_end_discards_immediately();
    test_abandoned_record_discards_immediately();
    test_mismatched_reply_is_fail_closed();
    test_unexpected_capability_is_fail_closed();
    test_malformed_length_is_fail_closed();
    test_supervisor_step_failure_is_fail_closed();
    test_supervisor_outage_retires_abandoned_read();
    test_foreign_reauth_result_survives_discard();
    return 0;
}
