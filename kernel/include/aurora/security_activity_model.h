#ifndef AURORA_SECURITY_ACTIVITY_MODEL_H
#define AURORA_SECURITY_ACTIVITY_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/identity_client.h>

#define AURORA_SECURITY_ACTIVITY_PAGE_CAPACITY 16u
#define AURORA_SECURITY_ACTIVITY_TEXT_KEY_MAX 48u

enum aurora_security_activity_category {
    AURORA_SECURITY_ACTIVITY_CATEGORY_AUTH = 1,
    AURORA_SECURITY_ACTIVITY_CATEGORY_SESSION,
    AURORA_SECURITY_ACTIVITY_CATEGORY_CREDENTIAL
};

enum aurora_security_activity_severity {
    AURORA_SECURITY_ACTIVITY_SEVERITY_INFO = 1,
    AURORA_SECURITY_ACTIVITY_SEVERITY_WARNING,
    AURORA_SECURITY_ACTIVITY_SEVERITY_CRITICAL
};

enum aurora_security_activity_view_state {
    AURORA_SECURITY_ACTIVITY_VIEW_IDLE = 0,
    AURORA_SECURITY_ACTIVITY_VIEW_LOADING,
    AURORA_SECURITY_ACTIVITY_VIEW_READY,
    AURORA_SECURITY_ACTIVITY_VIEW_EMPTY,
    AURORA_SECURITY_ACTIVITY_VIEW_END,
    AURORA_SECURITY_ACTIVITY_VIEW_ERROR
};

struct aurora_security_activity_item {
    uint64_t sequence;
    uint64_t monotonic_ms;
    uint64_t session_generation;
    uint32_t category;
    uint32_t severity;
    const char *title_key;
    const char *detail_key;
};

struct aurora_security_activity_page {
    struct aurora_security_activity_item items[AURORA_SECURITY_ACTIVITY_PAGE_CAPACITY];
    size_t count;
    uint64_t next_before_sequence;
    bool has_more;
};

bool security_activity_present_record(
    const struct aurora_security_activity_record *record,
    struct aurora_security_activity_item *out_item);

void security_activity_page_init(struct aurora_security_activity_page *page);

bool security_activity_page_append(
    struct aurora_security_activity_page *page,
    const struct aurora_security_activity_record *record);

#endif
