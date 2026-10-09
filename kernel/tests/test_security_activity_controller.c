/* Host-side regression: compile with
 * cc -std=c11 -Wall -Wextra -Werror -Ikernel/include \
 *   kernel/src/service/security_activity_controller.c \
 *   kernel/src/service/security_activity_model.c \
 *   kernel/tests/test_security_activity_controller.c -o /tmp/activity-controller-test
 * /tmp/activity-controller-test
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <aurora/security_activity_controller.h>
#include <aurora/identity_client.h>

static enum aurora_identity_client_state fake_state;
static enum aurora_identity_client_state begin_failure_state;
static struct aurora_security_activity_record fake_record;
static bool begin_succeeds;
static unsigned resets;
static unsigned discards;
static unsigned begin_calls;

enum aurora_identity_client_state identity_client_state(void) { return fake_state; }
void identity_client_pump(void) {}
void identity_client_reset_result(void) { ++resets; fake_state = AURORA_IDENTITY_CLIENT_READY; }
bool identity_client_discard_completed_security_activity(void) {
    ++discards;
    if (fake_state != AURORA_IDENTITY_CLIENT_ACTIVITY_RECORD &&
        fake_state != AURORA_IDENTITY_CLIENT_ACTIVITY_END) return false;
    fake_state = AURORA_IDENTITY_CLIENT_READY;
    return true;
}
bool identity_client_begin_security_activity_read(uint64_t before_sequence) {
    (void)before_sequence;
    ++begin_calls;
    if (!begin_succeeds) { fake_state = begin_failure_state; return false; }
    fake_state = AURORA_IDENTITY_CLIENT_READING_ACTIVITY;
    return true;
}
bool identity_client_take_security_activity_record(
    struct aurora_security_activity_record *record) {
    if (!record || fake_state != AURORA_IDENTITY_CLIENT_ACTIVITY_RECORD) return false;
    *record = fake_record;
    fake_state = AURORA_IDENTITY_CLIENT_READY;
    return true;
}
static void setup(void) {
    fake_state = AURORA_IDENTITY_CLIENT_READY;
    begin_failure_state = AURORA_IDENTITY_CLIENT_ERROR;
    begin_succeeds = true;
    resets = discards = begin_calls = 0;
    memset(&fake_record, 0, sizeof(fake_record));
    fake_record.record_version = 1;
    fake_record.event_type = 3;
    fake_record.outcome = 1;
    fake_record.sequence = 10;
}
static void test_initial_send_failure_recovery(void) {
    struct aurora_security_activity_controller ctl;
    setup();
    security_activity_controller_init(&ctl);
    begin_succeeds = false;
    assert(!security_activity_controller_begin(&ctl));
    assert(ctl.state == AURORA_SECURITY_ACTIVITY_VIEW_ERROR);
    assert(resets == 1 && fake_state == AURORA_IDENTITY_CLIENT_READY);
}
static void test_busy_client_not_reset(void) {
    struct aurora_security_activity_controller ctl;
    setup();
    security_activity_controller_init(&ctl);
    begin_succeeds = false;
    fake_state = AURORA_IDENTITY_CLIENT_REAUTH_VERIFIED;
    begin_failure_state = AURORA_IDENTITY_CLIENT_REAUTH_VERIFIED;
    assert(!security_activity_controller_begin(&ctl));
    assert(resets == 0 && fake_state == AURORA_IDENTITY_CLIENT_REAUTH_VERIFIED);
}
static void test_continuation_send_failure_recovery(void) {
    struct aurora_security_activity_controller ctl;
    setup();
    security_activity_controller_init(&ctl);
    assert(security_activity_controller_begin(&ctl));
    fake_state = AURORA_IDENTITY_CLIENT_ACTIVITY_RECORD;
    begin_succeeds = false;
    security_activity_controller_pump(&ctl);
    assert(ctl.state == AURORA_SECURITY_ACTIVITY_VIEW_ERROR);
    assert(ctl.page.count == 1 && ctl.page.items[0].sequence == 10);
    assert(resets == 1 && fake_state == AURORA_IDENTITY_CLIENT_READY);
}
static void test_bad_cursor_rejected(void) {
    struct aurora_security_activity_controller ctl;
    setup();
    security_activity_controller_init(&ctl);
    assert(security_activity_controller_begin(&ctl));
    fake_state = AURORA_IDENTITY_CLIENT_ACTIVITY_RECORD;
    security_activity_controller_pump(&ctl);
    assert(ctl.page.count == 1 && ctl.cursor == 10);
    fake_record.sequence = 10; /* repeats exclusive cursor */
    fake_state = AURORA_IDENTITY_CLIENT_ACTIVITY_RECORD;
    security_activity_controller_pump(&ctl);
    assert(ctl.state == AURORA_SECURITY_ACTIVITY_VIEW_ERROR);
    assert(ctl.page.count == 1);
}
static void test_end_of_activity_releases_result(void) {
    struct aurora_security_activity_controller ctl;
    setup();
    security_activity_controller_init(&ctl);
    assert(security_activity_controller_begin(&ctl));
    fake_state = AURORA_IDENTITY_CLIENT_ACTIVITY_END;
    security_activity_controller_pump(&ctl);
    assert(ctl.state == AURORA_SECURITY_ACTIVITY_VIEW_EMPTY);
    assert(ctl.reached_end && discards == 1 && resets == 0);
    assert(fake_state == AURORA_IDENTITY_CLIENT_READY);
}
static void test_failed_page_read_preserves_previous_page(void) {
    struct aurora_security_activity_controller ctl;
    setup();
    security_activity_controller_init(&ctl);
    assert(security_activity_controller_begin(&ctl));
    fake_state = AURORA_IDENTITY_CLIENT_ACTIVITY_RECORD;
    security_activity_controller_pump(&ctl);
    fake_state = AURORA_IDENTITY_CLIENT_ACTIVITY_END;
    security_activity_controller_pump(&ctl);
    assert(ctl.state == AURORA_SECURITY_ACTIVITY_VIEW_READY);
    assert(ctl.page.count == 1 && ctl.page.next_before_sequence == 10);
    assert(ctl.reached_end);
    fake_state = AURORA_IDENTITY_CLIENT_REAUTH_VERIFIED;
    begin_succeeds = false;
    begin_failure_state = AURORA_IDENTITY_CLIENT_REAUTH_VERIFIED;
    assert(!security_activity_controller_begin(&ctl));
    assert(ctl.page.count == 1 && ctl.page.items[0].sequence == 10);
    assert(fake_state == AURORA_IDENTITY_CLIENT_REAUTH_VERIFIED && resets == 0);
}
int main(void) {
    test_initial_send_failure_recovery();
    test_busy_client_not_reset();
    test_continuation_send_failure_recovery();
    test_bad_cursor_rejected();
    test_end_of_activity_releases_result();
    test_failed_page_read_preserves_previous_page();
    return 0;
}
