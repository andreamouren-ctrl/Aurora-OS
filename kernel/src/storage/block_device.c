#include <stddef.h>
#include <stdint.h>

#include <aurora/block_device.h>

#define BLOCK_TEST_SIZE 512u
#define BLOCK_TEST_COUNT 8u

struct memory_block_context {
    uint8_t *storage;
    size_t size;
    bool flushed;
};

static struct aurora_block_device *registry[AURORA_BLOCK_DEVICE_REGISTRY_MAX];
static size_t registry_count;

static bool strings_equal(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return false;
    }

    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return false;
        }
        ++a;
        ++b;
    }

    return *a == *b;
}

static bool range_valid(
    const struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count
) {
    if (device == NULL || block_count == 0u) {
        return false;
    }

    if (lba >= device->block_count) {
        return false;
    }

    uint64_t remaining = device->block_count - lba;
    return (uint64_t)block_count <= remaining;
}

bool block_device_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    if (buffer == NULL ||
        !range_valid(device, lba, block_count) ||
        device->read_blocks == NULL) {
        return false;
    }

    return device->read_blocks(device, lba, block_count, buffer);
}

bool block_device_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
) {
    if (buffer == NULL ||
        !range_valid(device, lba, block_count) ||
        device->read_only ||
        device->write_blocks == NULL) {
        return false;
    }

    return device->write_blocks(device, lba, block_count, buffer);
}

bool block_device_flush(struct aurora_block_device *device) {
    if (device == NULL) {
        return false;
    }

    if (device->flush == NULL) {
        return true;
    }

    return device->flush(device);
}

void block_device_registry_init(void) {
    for (size_t i = 0u; i < AURORA_BLOCK_DEVICE_REGISTRY_MAX; ++i) {
        registry[i] = NULL;
    }
    registry_count = 0u;
}

bool block_device_register(struct aurora_block_device *device) {
    if (device == NULL || device->name == NULL || device->name[0] == '\0' ||
        device->block_size == 0u || device->block_count == 0u ||
        device->read_blocks == NULL ||
        registry_count >= AURORA_BLOCK_DEVICE_REGISTRY_MAX) {
        return false;
    }

    for (size_t i = 0u; i < registry_count; ++i) {
        if (registry[i] == device ||
            strings_equal(registry[i]->name, device->name)) {
            return false;
        }
    }

    registry[registry_count++] = device;
    return true;
}

size_t block_device_count(void) {
    return registry_count;
}

struct aurora_block_device *block_device_get(size_t index) {
    return index < registry_count ? registry[index] : NULL;
}

struct aurora_block_device *block_device_find(const char *name) {
    if (name == NULL) {
        return NULL;
    }

    for (size_t i = 0u; i < registry_count; ++i) {
        if (strings_equal(registry[i]->name, name)) {
            return registry[i];
        }
    }

    return NULL;
}

static bool memory_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    struct memory_block_context *context =
        (struct memory_block_context *)device->context;

    if (context == NULL || context->storage == NULL) {
        return false;
    }

    size_t offset = (size_t)lba * device->block_size;
    size_t length = (size_t)block_count * device->block_size;

    if (offset > context->size || length > context->size - offset) {
        return false;
    }

    uint8_t *destination = (uint8_t *)buffer;

    for (size_t i = 0u; i < length; ++i) {
        destination[i] = context->storage[offset + i];
    }

    return true;
}

static bool memory_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
) {
    struct memory_block_context *context =
        (struct memory_block_context *)device->context;

    if (context == NULL || context->storage == NULL) {
        return false;
    }

    size_t offset = (size_t)lba * device->block_size;
    size_t length = (size_t)block_count * device->block_size;

    if (offset > context->size || length > context->size - offset) {
        return false;
    }

    const uint8_t *source = (const uint8_t *)buffer;

    for (size_t i = 0u; i < length; ++i) {
        context->storage[offset + i] = source[i];
    }

    context->flushed = false;
    return true;
}

static bool memory_flush(struct aurora_block_device *device) {
    struct memory_block_context *context =
        (struct memory_block_context *)device->context;

    if (context == NULL) {
        return false;
    }

    context->flushed = true;
    return true;
}

bool block_device_self_test(void) {
    static uint8_t storage[BLOCK_TEST_SIZE * BLOCK_TEST_COUNT];
    uint8_t write_buffer[BLOCK_TEST_SIZE];
    uint8_t read_buffer[BLOCK_TEST_SIZE];

    struct memory_block_context context = {
        .storage = storage,
        .size = sizeof(storage),
        .flushed = false
    };

    struct aurora_block_device device = {
        .name = "bootstrap-memory-block",
        .block_size = BLOCK_TEST_SIZE,
        .block_count = BLOCK_TEST_COUNT,
        .read_only = false,
        .context = &context,
        .read_blocks = memory_read,
        .write_blocks = memory_write,
        .flush = memory_flush
    };

    for (size_t i = 0u; i < sizeof(write_buffer); ++i) {
        write_buffer[i] = (uint8_t)((i * 37u + 0x41u) & 0xFFu);
        read_buffer[i] = 0u;
    }

    if (!block_device_write(&device, 3u, 1u, write_buffer)) {
        return false;
    }

    if (context.flushed || !block_device_flush(&device) || !context.flushed) {
        return false;
    }

    if (!block_device_read(&device, 3u, 1u, read_buffer)) {
        return false;
    }

    for (size_t i = 0u; i < sizeof(write_buffer); ++i) {
        if (read_buffer[i] != write_buffer[i]) {
            return false;
        }
    }

    if (block_device_read(&device, BLOCK_TEST_COUNT, 1u, read_buffer)) {
        return false;
    }

    block_device_registry_init();
    if (!block_device_register(&device) ||
        block_device_count() != 1u ||
        block_device_get(0u) != &device ||
        block_device_find("bootstrap-memory-block") != &device ||
        block_device_register(&device)) {
        block_device_registry_init();
        return false;
    }

    device.read_only = true;
    if (block_device_write(&device, 0u, 1u, write_buffer)) {
        block_device_registry_init();
        return false;
    }

    block_device_registry_init();
    return true;
}
