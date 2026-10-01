#include <stddef.h>

#include <aurora/fs_driver.h>

static const struct aurora_fs_driver *drivers[AURORA_FS_DRIVER_MAX];
static size_t driver_count;

void fs_driver_registry_init(void) {
    for (size_t i = 0u; i < AURORA_FS_DRIVER_MAX; ++i) {
        drivers[i] = NULL;
    }
    driver_count = 0u;
}

bool fs_driver_register(const struct aurora_fs_driver *driver) {
    if (driver == NULL || driver->name == NULL || driver->probe == NULL ||
        driver_count >= AURORA_FS_DRIVER_MAX) {
        return false;
    }

    for (size_t i = 0u; i < driver_count; ++i) {
        if (drivers[i] == driver) {
            return true;
        }
    }

    drivers[driver_count++] = driver;
    return true;
}

size_t fs_driver_count(void) {
    return driver_count;
}

const struct aurora_fs_driver *fs_driver_at(size_t index) {
    if (index >= driver_count) {
        return NULL;
    }
    return drivers[index];
}

bool fs_driver_detect(
    const struct aurora_partition *partition,
    struct aurora_fs_match *out_match
) {
    if (partition == NULL || out_match == NULL) {
        return false;
    }

    out_match->driver = NULL;
    out_match->access = AURORA_FS_PROBE_NO_MATCH;

    for (size_t i = 0u; i < driver_count; ++i) {
        const struct aurora_fs_driver *driver = drivers[i];
        enum aurora_fs_probe_result result = driver->probe(partition);
        if (result != AURORA_FS_PROBE_NO_MATCH) {
            out_match->driver = driver;
            out_match->access = result;
            return true;
        }
    }

    return false;
}
