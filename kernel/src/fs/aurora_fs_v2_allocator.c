#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs_v2.h>
#include <aurora/block_device.h>

#define V2_BITS_PER_BITMAP_BLOCK ((uint64_t)AURORA_FS_V2_FS_BLOCK_SIZE * 8u)
#define V2_ALLOCATOR_TEST_TOTAL_BLOCKS 70000u
#define V2_ALLOCATOR_TEST_BITMAP_BLOCKS 3u
#define V2_ALLOCATOR_TEST_DATA_START 64u
#define V2_ALLOCATOR_TEST_BITMAP_START 1u
#define V2_ALLOCATOR_TEST_SECOND_BLOCK_PREFIX 23u

struct allocator_test_context {
    uint32_t device_block_size;
    uint64_t bitmap_byte_start;
    uint64_t bitmap_byte_length;
    uint8_t bitmap[V2_ALLOCATOR_TEST_BITMAP_BLOCKS][AURORA_FS_V2_FS_BLOCK_SIZE];
};

static uint8_t bitmap_scratch[AURORA_FS_V2_FS_BLOCK_SIZE];
static struct allocator_test_context allocator_test_context;

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = buffer;
    for (size_t i = 0u; i < length; ++i) {
        bytes[i] = 0u;
    }
}

static bool add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || b > UINT64_MAX - a) {
        return false;
    }
    *out = a + b;
    return true;
}

static bool mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || (a != 0u && b > UINT64_MAX / a)) {
        return false;
    }
    *out = a * b;
    return true;
}

static bool bitmap_geometry_valid(const struct aurora_fs_v2_allocator *allocator) {
    if (allocator == NULL || allocator->device == NULL ||
        allocator->device->block_size == 0u ||
        allocator->device->block_size > AURORA_FS_V2_FS_BLOCK_SIZE ||
        (AURORA_FS_V2_FS_BLOCK_SIZE % allocator->device->block_size) != 0u ||
        allocator->bitmap_blocks == 0u ||
        allocator->data_start >= allocator->total_fs_blocks) {
        return false;
    }

    uint64_t capacity;
    return mul_u64(allocator->bitmap_blocks, V2_BITS_PER_BITMAP_BLOCK, &capacity) &&
        capacity >= allocator->total_fs_blocks;
}

static bool bitmap_block_device_lba(
    const struct aurora_fs_v2_allocator *allocator,
    uint64_t bitmap_block_index,
    uint64_t *out_lba,
    uint32_t *out_device_blocks
) {
    if (!bitmap_geometry_valid(allocator) || out_lba == NULL ||
        out_device_blocks == NULL || bitmap_block_index >= allocator->bitmap_blocks) {
        return false;
    }

    uint64_t fs_block;
    uint64_t byte_offset;
    uint64_t bitmap_offset;
    if (!add_u64(allocator->bitmap_start, bitmap_block_index, &fs_block) ||
        !mul_u64(fs_block, AURORA_FS_V2_FS_BLOCK_SIZE, &bitmap_offset) ||
        !add_u64(allocator->base_bytes, bitmap_offset, &byte_offset) ||
        (byte_offset % allocator->device->block_size) != 0u) {
        return false;
    }

    uint64_t count = AURORA_FS_V2_FS_BLOCK_SIZE / allocator->device->block_size;
    if (count == 0u || count > UINT32_MAX) {
        return false;
    }

    *out_lba = byte_offset / allocator->device->block_size;
    *out_device_blocks = (uint32_t)count;
    return true;
}

static bool read_bitmap_block(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t bitmap_block_index,
    uint8_t *buffer
) {
    uint64_t lba;
    uint32_t device_blocks;
    return buffer != NULL &&
        bitmap_block_device_lba(allocator, bitmap_block_index, &lba, &device_blocks) &&
        block_device_read(allocator->device, lba, device_blocks, buffer);
}

static bool write_bitmap_block(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t bitmap_block_index,
    const uint8_t *buffer
) {
    uint64_t lba;
    uint32_t device_blocks;
    return buffer != NULL && !allocator->device->read_only &&
        bitmap_block_device_lba(allocator, bitmap_block_index, &lba, &device_blocks) &&
        block_device_write(allocator->device, lba, device_blocks, buffer);
}

static bool bit_value(const uint8_t *bitmap, uint64_t local_bit) {
    return (bitmap[local_bit >> 3] & (uint8_t)(1u << (local_bit & 7u))) != 0u;
}

static void set_bit_value(uint8_t *bitmap, uint64_t local_bit, bool allocated) {
    uint8_t mask = (uint8_t)(1u << (local_bit & 7u));
    if (allocated) {
        bitmap[local_bit >> 3] |= mask;
    } else {
        bitmap[local_bit >> 3] &= (uint8_t)~mask;
    }
}

static bool mark_range(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t first_block,
    uint64_t block_count,
    bool allocated
) {
    if (!bitmap_geometry_valid(allocator) || block_count == 0u ||
        first_block < allocator->data_start || first_block >= allocator->total_fs_blocks ||
        block_count > allocator->total_fs_blocks - first_block) {
        return false;
    }

    uint64_t current = first_block;
    uint64_t remaining = block_count;
    while (remaining != 0u) {
        uint64_t bitmap_index = current / V2_BITS_PER_BITMAP_BLOCK;
        uint64_t local_bit = current % V2_BITS_PER_BITMAP_BLOCK;
        uint64_t available = V2_BITS_PER_BITMAP_BLOCK - local_bit;
        uint64_t take = remaining < available ? remaining : available;

        if (!read_bitmap_block(allocator, bitmap_index, bitmap_scratch)) {
            return false;
        }
        for (uint64_t i = 0u; i < take; ++i) {
            set_bit_value(bitmap_scratch, local_bit + i, allocated);
        }
        if (!write_bitmap_block(allocator, bitmap_index, bitmap_scratch)) {
            return false;
        }

        current += take;
        remaining -= take;
    }

    return block_device_flush(allocator->device);
}

bool aurora_fs_v2_allocator_init(
    struct aurora_fs_v2_allocator *allocator,
    struct aurora_block_device *device,
    uint64_t base_bytes,
    uint64_t total_fs_blocks,
    uint64_t bitmap_start,
    uint64_t bitmap_blocks,
    uint64_t data_start
) {
    if (allocator == NULL || device == NULL || total_fs_blocks == 0u) {
        return false;
    }

    allocator->device = device;
    allocator->base_bytes = base_bytes;
    allocator->total_fs_blocks = total_fs_blocks;
    allocator->bitmap_start = bitmap_start;
    allocator->bitmap_blocks = bitmap_blocks;
    allocator->data_start = data_start;
    return bitmap_geometry_valid(allocator);
}

bool aurora_fs_v2_allocator_is_allocated(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t fs_block,
    bool *out_allocated
) {
    if (!bitmap_geometry_valid(allocator) || out_allocated == NULL ||
        fs_block >= allocator->total_fs_blocks) {
        return false;
    }

    uint64_t bitmap_index = fs_block / V2_BITS_PER_BITMAP_BLOCK;
    uint64_t local_bit = fs_block % V2_BITS_PER_BITMAP_BLOCK;
    if (!read_bitmap_block(allocator, bitmap_index, bitmap_scratch)) {
        return false;
    }

    *out_allocated = bit_value(bitmap_scratch, local_bit);
    return true;
}

bool aurora_fs_v2_allocator_allocate_range(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t block_count,
    uint64_t *out_first_block
) {
    if (!bitmap_geometry_valid(allocator) || allocator->device->read_only ||
        block_count == 0u || out_first_block == NULL ||
        block_count > allocator->total_fs_blocks - allocator->data_start) {
        return false;
    }

    uint64_t run_start = 0u;
    uint64_t run_length = 0u;
    for (uint64_t bitmap_index = 0u;
         bitmap_index < allocator->bitmap_blocks;
         ++bitmap_index) {
        if (!read_bitmap_block(allocator, bitmap_index, bitmap_scratch)) {
            return false;
        }

        uint64_t block_base = bitmap_index * V2_BITS_PER_BITMAP_BLOCK;
        uint64_t limit = V2_BITS_PER_BITMAP_BLOCK;
        if (block_base >= allocator->total_fs_blocks) {
            break;
        }
        if (limit > allocator->total_fs_blocks - block_base) {
            limit = allocator->total_fs_blocks - block_base;
        }

        for (uint64_t local_bit = 0u; local_bit < limit; ++local_bit) {
            uint64_t fs_block = block_base + local_bit;
            if (fs_block < allocator->data_start || bit_value(bitmap_scratch, local_bit)) {
                run_length = 0u;
                continue;
            }

            if (run_length == 0u) {
                run_start = fs_block;
            }
            ++run_length;
            if (run_length == block_count) {
                if (!mark_range(allocator, run_start, block_count, true)) {
                    return false;
                }
                *out_first_block = run_start;
                return true;
            }
        }
    }

    return false;
}

bool aurora_fs_v2_allocator_free_range(
    struct aurora_fs_v2_allocator *allocator,
    uint64_t first_block,
    uint64_t block_count
) {
    if (!bitmap_geometry_valid(allocator) || allocator->device->read_only ||
        block_count == 0u || first_block < allocator->data_start ||
        first_block >= allocator->total_fs_blocks ||
        block_count > allocator->total_fs_blocks - first_block) {
        return false;
    }

    for (uint64_t block = first_block; block < first_block + block_count; ++block) {
        bool allocated = false;
        if (!aurora_fs_v2_allocator_is_allocated(allocator, block, &allocated) || !allocated) {
            return false;
        }
    }
    return mark_range(allocator, first_block, block_count, false);
}

static bool test_sparse_read(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    void *buffer
) {
    if (device == NULL || buffer == NULL || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba) {
        return false;
    }

    struct allocator_test_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length)) {
        return false;
    }

    uint8_t *out = buffer;
    zero_bytes(out, (size_t)length);
    uint64_t bitmap_end = context->bitmap_byte_start + context->bitmap_byte_length;
    uint64_t request_end = offset + length;
    if (request_end <= context->bitmap_byte_start || offset >= bitmap_end) {
        return true;
    }

    uint64_t copy_start = offset > context->bitmap_byte_start ? offset : context->bitmap_byte_start;
    uint64_t copy_end = request_end < bitmap_end ? request_end : bitmap_end;
    uint64_t source_offset = copy_start - context->bitmap_byte_start;
    uint64_t dest_offset = copy_start - offset;
    const uint8_t *source = &context->bitmap[0][0];
    for (uint64_t i = 0u; i < copy_end - copy_start; ++i) {
        out[dest_offset + i] = source[source_offset + i];
    }
    return true;
}

static bool test_sparse_write(
    struct aurora_block_device *device,
    uint64_t lba,
    uint32_t block_count,
    const void *buffer
) {
    if (device == NULL || buffer == NULL || device->read_only || block_count == 0u ||
        lba >= device->block_count || (uint64_t)block_count > device->block_count - lba) {
        return false;
    }

    struct allocator_test_context *context = device->context;
    uint64_t offset;
    uint64_t length;
    if (context == NULL || !mul_u64(lba, device->block_size, &offset) ||
        !mul_u64(block_count, device->block_size, &length)) {
        return false;
    }

    uint64_t bitmap_end = context->bitmap_byte_start + context->bitmap_byte_length;
    uint64_t request_end = offset + length;
    if (offset < context->bitmap_byte_start || request_end > bitmap_end) {
        return false;
    }

    uint64_t dest_offset = offset - context->bitmap_byte_start;
    const uint8_t *source = buffer;
    uint8_t *dest = &context->bitmap[0][0];
    for (uint64_t i = 0u; i < length; ++i) {
        dest[dest_offset + i] = source[i];
    }
    return true;
}

static bool test_sparse_flush(struct aurora_block_device *device) {
    return device != NULL;
}

static void test_raw_set(uint64_t fs_block, bool allocated) {
    uint64_t bitmap_index = fs_block / V2_BITS_PER_BITMAP_BLOCK;
    uint64_t local_bit = fs_block % V2_BITS_PER_BITMAP_BLOCK;
    set_bit_value(allocator_test_context.bitmap[bitmap_index], local_bit, allocated);
}

static bool run_allocator_geometry(uint32_t device_block_size) {
    zero_bytes(&allocator_test_context, sizeof(allocator_test_context));
    allocator_test_context.device_block_size = device_block_size;
    allocator_test_context.bitmap_byte_start =
        AURORA_FS_V2_DEFAULT_BASE_BYTES +
        V2_ALLOCATOR_TEST_BITMAP_START * AURORA_FS_V2_FS_BLOCK_SIZE;
    allocator_test_context.bitmap_byte_length =
        V2_ALLOCATOR_TEST_BITMAP_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;

    uint64_t virtual_bytes =
        AURORA_FS_V2_DEFAULT_BASE_BYTES +
        V2_ALLOCATOR_TEST_TOTAL_BLOCKS * AURORA_FS_V2_FS_BLOCK_SIZE;
    struct aurora_block_device device = {
        .name = "aurorafs-v2-allocator-test",
        .block_size = device_block_size,
        .block_count = virtual_bytes / device_block_size,
        .read_only = false,
        .context = &allocator_test_context,
        .read_blocks = test_sparse_read,
        .write_blocks = test_sparse_write,
        .flush = test_sparse_flush
    };

    for (uint64_t block = 0u; block < V2_ALLOCATOR_TEST_DATA_START; ++block) {
        test_raw_set(block, true);
    }
    for (uint64_t block = V2_ALLOCATOR_TEST_DATA_START;
         block < V2_BITS_PER_BITMAP_BLOCK + V2_ALLOCATOR_TEST_SECOND_BLOCK_PREFIX;
         ++block) {
        test_raw_set(block, true);
    }

    struct aurora_fs_v2_allocator allocator;
    if (!aurora_fs_v2_allocator_init(
            &allocator,
            &device,
            AURORA_FS_V2_DEFAULT_BASE_BYTES,
            V2_ALLOCATOR_TEST_TOTAL_BLOCKS,
            V2_ALLOCATOR_TEST_BITMAP_START,
            V2_ALLOCATOR_TEST_BITMAP_BLOCKS,
            V2_ALLOCATOR_TEST_DATA_START)) {
        return false;
    }

    uint64_t expected = V2_BITS_PER_BITMAP_BLOCK + V2_ALLOCATOR_TEST_SECOND_BLOCK_PREFIX;
    uint64_t allocated_first = 0u;
    if (!aurora_fs_v2_allocator_allocate_range(&allocator, 7u, &allocated_first) ||
        allocated_first != expected) {
        return false;
    }

    for (uint64_t i = 0u; i < 7u; ++i) {
        bool allocated = false;
        if (!aurora_fs_v2_allocator_is_allocated(&allocator, allocated_first + i, &allocated) ||
            !allocated) {
            return false;
        }
    }

    struct aurora_fs_v2_allocator reopened;
    if (!aurora_fs_v2_allocator_init(
            &reopened,
            &device,
            AURORA_FS_V2_DEFAULT_BASE_BYTES,
            V2_ALLOCATOR_TEST_TOTAL_BLOCKS,
            V2_ALLOCATOR_TEST_BITMAP_START,
            V2_ALLOCATOR_TEST_BITMAP_BLOCKS,
            V2_ALLOCATOR_TEST_DATA_START)) {
        return false;
    }

    bool persisted = false;
    if (!aurora_fs_v2_allocator_is_allocated(&reopened, allocated_first + 6u, &persisted) ||
        !persisted ||
        !aurora_fs_v2_allocator_free_range(&reopened, allocated_first, 7u)) {
        return false;
    }

    for (uint64_t i = 0u; i < 7u; ++i) {
        bool allocated = true;
        if (!aurora_fs_v2_allocator_is_allocated(&reopened, allocated_first + i, &allocated) ||
            allocated) {
            return false;
        }
    }

    /* Explicitly verify a range that crosses bitmap block 0 -> 1. */
    uint64_t crossing = V2_BITS_PER_BITMAP_BLOCK - 3u;
    for (uint64_t i = 0u; i < 6u; ++i) {
        test_raw_set(crossing + i, false);
    }
    uint64_t crossing_result = 0u;
    if (!aurora_fs_v2_allocator_allocate_range(&reopened, 6u, &crossing_result) ||
        crossing_result != crossing) {
        return false;
    }

    return aurora_fs_v2_allocator_free_range(&reopened, crossing_result, 6u);
}

bool aurora_fs_v2_allocator_self_test(void) {
    return run_allocator_geometry(512u) && run_allocator_geometry(4096u);
}
