#include <stddef.h>
#include <stdint.h>

#include <aurora/identity_service_protocol.h>
#include <aurora/security_activity_model.h>

static bool map_event(
    const struct aurora_security_activity_record *record,
    struct aurora_security_activity_item *item
) {
    switch (record->event_type) {
        case 1u:
            item->category = AURORA_SECURITY_ACTIVITY_CATEGORY_AUTH;
            item->severity = AURORA_SECURITY_ACTIVITY_SEVERITY_INFO;
            item->title_key = "identity.activity.auth.success.title";
            item->detail_key = "identity.activity.auth.success.detail";
            return true;
        case 2u:
            item->category = AURORA_SECURITY_ACTIVITY_CATEGORY_AUTH;
            item->severity =
                record->outcome == 4u
                    ? AURORA_SECURITY_ACTIVITY_SEVERITY_WARNING
                    : AURORA_SECURITY_ACTIVITY_SEVERITY_WARNING;
            item->title_key = "identity.activity.auth.failure.title";
            item->detail_key = "identity.activity.auth.failure.detail";
            return true;
        case 3u:
            item->category = AURORA_SECURITY_ACTIVITY_CATEGORY_SESSION;
            item->severity = AURORA_SECURITY_ACTIVITY_SEVERITY_INFO;
            item->title_key = "identity.activity.session.started.title";
            item->detail_key = "identity.activity.session.started.detail";
            return true;
        case 4u:
            item->category = AURORA_SECURITY_ACTIVITY_CATEGORY_SESSION;
            item->severity = AURORA_SECURITY_ACTIVITY_SEVERITY_INFO;
            item->title_key = "identity.activity.session.locked.title";
            item->detail_key = "identity.activity.session.locked.detail";
            return true;
        case 5u:
            item->category = AURORA_SECURITY_ACTIVITY_CATEGORY_SESSION;
            item->severity = AURORA_SECURITY_ACTIVITY_SEVERITY_INFO;
            item->title_key = "identity.activity.session.unlocked.title";
            item->detail_key = "identity.activity.session.unlocked.detail";
            return true;
        case 6u:
            item->category = AURORA_SECURITY_ACTIVITY_CATEGORY_SESSION;
            item->severity = AURORA_SECURITY_ACTIVITY_SEVERITY_INFO;
            item->title_key = "identity.activity.session.logout.title";
            item->detail_key = "identity.activity.session.logout.detail";
            return true;
        case 7u:
            item->category = AURORA_SECURITY_ACTIVITY_CATEGORY_SESSION;
            item->severity = AURORA_SECURITY_ACTIVITY_SEVERITY_WARNING;
            item->title_key = "identity.activity.session.terminated.title";
            item->detail_key = "identity.activity.session.terminated.detail";
            return true;
        case 8u:
            item->category = AURORA_SECURITY_ACTIVITY_CATEGORY_AUTH;
            item->severity = AURORA_SECURITY_ACTIVITY_SEVERITY_INFO;
            item->title_key = "identity.activity.reauth.success.title";
            item->detail_key = "identity.activity.reauth.success.detail";
            return true;
        case 9u:
            item->category = AURORA_SECURITY_ACTIVITY_CATEGORY_AUTH;
            item->severity = AURORA_SECURITY_ACTIVITY_SEVERITY_WARNING;
            item->title_key = "identity.activity.reauth.failure.title";
            item->detail_key = "identity.activity.reauth.failure.detail";
            return true;
        case 10u:
            item->category = AURORA_SECURITY_ACTIVITY_CATEGORY_CREDENTIAL;
            item->severity =
                record->outcome == 1u
                    ? AURORA_SECURITY_ACTIVITY_SEVERITY_INFO
                    : AURORA_SECURITY_ACTIVITY_SEVERITY_WARNING;
            item->title_key = "identity.activity.credential.rotated.title";
            item->detail_key = "identity.activity.credential.rotated.detail";
            return true;
        default:
            return false;
    }
}

bool security_activity_present_record(
    const struct aurora_security_activity_record *record,
    struct aurora_security_activity_item *out_item
) {
    if (record == NULL || out_item == NULL ||
        record->record_version == 0u || record->sequence == 0u) {
        return false;
    }

    *out_item = (struct aurora_security_activity_item){0};
    if (!map_event(record, out_item)) return false;

    out_item->sequence = record->sequence;
    out_item->monotonic_ms = record->monotonic_ms;
    out_item->session_generation = record->session_generation;
    return true;
}

void security_activity_page_init(struct aurora_security_activity_page *page) {
    if (page == NULL) return;
    *page = (struct aurora_security_activity_page){0};
}

bool security_activity_page_append(
    struct aurora_security_activity_page *page,
    const struct aurora_security_activity_record *record
) {
    if (page == NULL || record == NULL ||
        page->count >= AURORA_SECURITY_ACTIVITY_PAGE_CAPACITY) {
        return false;
    }

    struct aurora_security_activity_item item;
    if (!security_activity_present_record(record, &item)) return false;

    if (page->count != 0u &&
        item.sequence >= page->items[page->count - 1u].sequence) {
        return false;
    }

    page->items[page->count++] = item;
    page->next_before_sequence = item.sequence;
    page->has_more = page->count == AURORA_SECURITY_ACTIVITY_PAGE_CAPACITY;
    return true;
}
