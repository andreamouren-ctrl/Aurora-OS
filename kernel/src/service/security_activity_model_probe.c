#include <stdint.h>
#include <aurora/security_activity_model.h>
#include <aurora/security_activity_model_probe.h>

bool security_activity_model_self_test(void) {
    struct aurora_security_activity_page page;
    struct aurora_security_activity_record newest = {
        .record_version = 1u,
        .event_type = 7u,
        .outcome = 1u,
        .sequence = 42u,
        .monotonic_ms = 1000u,
        .session_generation = 9u
    };
    struct aurora_security_activity_record older = {
        .record_version = 1u,
        .event_type = 3u,
        .outcome = 1u,
        .sequence = 40u,
        .monotonic_ms = 900u,
        .session_generation = 8u
    };
    struct aurora_security_activity_record invalid = {
        .record_version = 1u,
        .event_type = 99u,
        .outcome = 1u,
        .sequence = 39u
    };

    security_activity_page_init(&page);
    if (!security_activity_page_append(&page, &newest) ||
        !security_activity_page_append(&page, &older) ||
        security_activity_page_append(&page, &invalid) ||
        page.count != 2u ||
        page.next_before_sequence != 40u ||
        page.items[0].category != AURORA_SECURITY_ACTIVITY_CATEGORY_SESSION ||
        page.items[0].severity != AURORA_SECURITY_ACTIVITY_SEVERITY_WARNING ||
        page.items[1].severity != AURORA_SECURITY_ACTIVITY_SEVERITY_INFO) {
        return false;
    }

    return true;
}
