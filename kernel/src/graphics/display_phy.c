#include <stddef.h>
#include <stdint.h>

#include <aurora/display_phy.h>

bool display_phy_bring_up(
    struct aurora_display_phy *phy,
    enum aurora_link_kind kind,
    uint32_t link_rate,
    uint8_t lane_count,
    uint32_t max_training_attempts
) {
    if (phy == NULL ||
        phy->ops == NULL ||
        phy->ops->prepare == NULL ||
        phy->ops->shutdown == NULL ||
        phy->ops->train_step == NULL ||
        phy->active ||
        kind == AURORA_LINK_NONE ||
        link_rate == 0u ||
        lane_count == 0u ||
        lane_count > 4u) {
        return false;
    }

    if (!phy->ops->prepare(
            phy->context,
            kind,
            link_rate,
            lane_count)) {
        return false;
    }

    struct aurora_link_training_session session;
    struct aurora_link_training_backend backend = {
        .step = phy->ops->train_step,
        .context = phy->context
    };

    if (!display_link_training_start(
            &session,
            kind,
            max_training_attempts) ||
        !display_link_training_run(
            &session,
            &backend)) {
        (void)phy->ops->shutdown(phy->context);
        return false;
    }

    phy->active_kind = kind;
    phy->link_rate = link_rate;
    phy->lane_count = lane_count;
    phy->active = true;
    return true;
}

bool display_phy_shut_down(
    struct aurora_display_phy *phy
) {
    if (phy == NULL ||
        phy->ops == NULL ||
        phy->ops->shutdown == NULL ||
        !phy->active) {
        return false;
    }

    if (!phy->ops->shutdown(phy->context)) {
        return false;
    }

    phy->active_kind = AURORA_LINK_NONE;
    phy->link_rate = 0u;
    phy->lane_count = 0u;
    phy->active = false;
    return true;
}

struct phy_probe {
    uint32_t prepare_calls;
    uint32_t train_calls;
    uint32_t shutdown_calls;
};

static bool probe_prepare(
    void *context,
    enum aurora_link_kind kind,
    uint32_t link_rate,
    uint8_t lane_count
) {
    struct phy_probe *probe =
        (struct phy_probe *)context;

    if (probe == NULL ||
        kind != AURORA_LINK_DISPLAYPORT ||
        link_rate != 8100u ||
        lane_count != 4u) {
        return false;
    }

    ++probe->prepare_calls;
    return true;
}

static bool probe_training(
    void *context,
    enum aurora_link_training_state state,
    uint32_t attempt
) {
    struct phy_probe *probe =
        (struct phy_probe *)context;

    if (probe == NULL) return false;
    ++probe->train_calls;

    if (state == AURORA_LINK_TRAINING_PREPARE ||
        state == AURORA_LINK_TRAINING_CHANNEL_EQUALIZATION ||
        state == AURORA_LINK_TRAINING_ACTIVE) {
        return true;
    }

    if (state == AURORA_LINK_TRAINING_CLOCK_RECOVERY) {
        return attempt >= 2u;
    }

    return false;
}

static bool probe_shutdown(void *context) {
    struct phy_probe *probe =
        (struct phy_probe *)context;

    if (probe == NULL) return false;
    ++probe->shutdown_calls;
    return true;
}

bool display_phy_selftest(void) {
    struct phy_probe probe = {0};

    static const struct aurora_display_phy_ops ops = {
        .prepare = probe_prepare,
        .shutdown = probe_shutdown,
        .train_step = probe_training
    };

    struct aurora_display_phy phy = {
        .ops = &ops,
        .context = &probe
    };

    if (!display_phy_bring_up(
            &phy,
            AURORA_LINK_DISPLAYPORT,
            8100u,
            4u,
            4u) ||
        !phy.active ||
        phy.active_kind != AURORA_LINK_DISPLAYPORT ||
        phy.link_rate != 8100u ||
        phy.lane_count != 4u ||
        probe.prepare_calls != 1u ||
        probe.train_calls != 5u ||
        !display_phy_shut_down(&phy) ||
        phy.active ||
        probe.shutdown_calls != 1u) {
        return false;
    }

    return true;
}
