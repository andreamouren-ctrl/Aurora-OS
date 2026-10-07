#ifndef AURORA_DISPLAY_LINK_TRAINING_H
#define AURORA_DISPLAY_LINK_TRAINING_H

#include <stdbool.h>
#include <stdint.h>

enum aurora_link_kind {
    AURORA_LINK_NONE = 0,
    AURORA_LINK_DISPLAYPORT,
    AURORA_LINK_HDMI_TMDS,
    AURORA_LINK_HDMI_FRL
};

enum aurora_link_training_state {
    AURORA_LINK_TRAINING_IDLE = 0,
    AURORA_LINK_TRAINING_PREPARE,
    AURORA_LINK_TRAINING_CLOCK_RECOVERY,
    AURORA_LINK_TRAINING_CHANNEL_EQUALIZATION,
    AURORA_LINK_TRAINING_FRL_PATTERN,
    AURORA_LINK_TRAINING_ACTIVE,
    AURORA_LINK_TRAINING_FAILED
};

typedef bool (*aurora_link_training_step_fn)(
    void *context,
    enum aurora_link_training_state state,
    uint32_t attempt
);

struct aurora_link_training_backend {
    aurora_link_training_step_fn step;
    void *context;
};

struct aurora_link_training_session {
    enum aurora_link_kind kind;
    enum aurora_link_training_state state;
    uint32_t attempts;
    uint32_t max_attempts;
};

bool display_link_training_start(
    struct aurora_link_training_session *session,
    enum aurora_link_kind kind,
    uint32_t max_attempts
);

bool display_link_training_run(
    struct aurora_link_training_session *session,
    const struct aurora_link_training_backend *backend
);

bool display_link_training_selftest(void);

#endif
