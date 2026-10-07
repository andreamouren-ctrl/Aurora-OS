#ifndef AURORA_DISPLAY_PHY_H
#define AURORA_DISPLAY_PHY_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/display_link_training.h>

typedef bool (*aurora_display_phy_prepare_fn)(
    void *context,
    enum aurora_link_kind kind,
    uint32_t link_rate,
    uint8_t lane_count
);

typedef bool (*aurora_display_phy_shutdown_fn)(
    void *context
);

struct aurora_display_phy_ops {
    aurora_display_phy_prepare_fn prepare;
    aurora_display_phy_shutdown_fn shutdown;
    aurora_link_training_step_fn train_step;
};

struct aurora_display_phy {
    const struct aurora_display_phy_ops *ops;
    void *context;
    enum aurora_link_kind active_kind;
    uint32_t link_rate;
    uint8_t lane_count;
    bool active;
};

bool display_phy_bring_up(
    struct aurora_display_phy *phy,
    enum aurora_link_kind kind,
    uint32_t link_rate,
    uint8_t lane_count,
    uint32_t max_training_attempts
);

bool display_phy_shut_down(
    struct aurora_display_phy *phy
);

bool display_phy_selftest(void);

#endif
