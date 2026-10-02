#include <stddef.h>
#include <stdint.h>

#include <aurora/aurora_fs.h>
#include <aurora/aurora_fs_v2.h>
#include <aurora/aurora_fs_v2_inode_publish.h>
#include <aurora/block_device.h>
#include <aurora/fs_driver.h>
#include <aurora/log.h>
#include <aurora/partition.h>

#define AURORA_FS_TEST_BLOCK_SIZE 4096u
#define AURORA_FS_TEST_BLOCK_COUNT 32u

static uint8_t test_storage[AURORA_FS_TEST_BLOCK_SIZE * AURORA_FS_TEST_BLOCK_COUNT];

static bool test_read(struct aurora_block_device *device, uint64_t lba, uint32_t block_count, void *buffer) {
    (void)device;
    if (buffer == NULL || lba > AURORA_FS_TEST_BLOCK_COUNT || block_count > AURORA_FS_TEST_BLOCK_COUNT - lba) return false;
    uint8_t *out = (uint8_t *)buffer;
    size_t offset = (size_t)lba * AURORA_FS_TEST_BLOCK_SIZE;
    size_t length = (size_t)block_count * AURORA_FS_TEST_BLOCK_SIZE;
    for (size_t i = 0u; i < length; ++i) out[i] = test_storage[offset + i];
    return true;
}

static bool test_write(struct aurora_block_device *device, uint64_t lba, uint32_t block_count, const void *buffer) {
    (void)device;
    if (buffer == NULL || lba > AURORA_FS_TEST_BLOCK_COUNT || block_count > AURORA_FS_TEST_BLOCK_COUNT - lba) return false;
    const uint8_t *in = (const uint8_t *)buffer;
    size_t offset = (size_t)lba * AURORA_FS_TEST_BLOCK_SIZE;
    size_t length = (size_t)block_count * AURORA_FS_TEST_BLOCK_SIZE;
    for (size_t i = 0u; i < length; ++i) test_storage[offset + i] = in[i];
    return true;
}

static bool test_flush(struct aurora_block_device *device) { (void)device; return true; }
static void clear_storage(void) { for (size_t i = 0u; i < sizeof(test_storage); ++i) test_storage[i] = 0u; }
static bool bytes_equal(const uint8_t *a, const uint8_t *b, size_t length) {
    for (size_t i = 0u; i < length; ++i) if (a[i] != b[i]) return false;
    return true;
}

bool aurora_fs_4kn_self_test(void) {
    clear_storage();
    struct aurora_block_device device = { .name = "aurorafs-4kn-self-test", .block_size = AURORA_FS_TEST_BLOCK_SIZE, .block_count = AURORA_FS_TEST_BLOCK_COUNT, .read_only = false, .context = NULL, .read_blocks = test_read, .write_blocks = test_write, .flush = test_flush };
    struct aurora_fs_bootstrap_result first = { 0 };
    if (!aurora_fs_bootstrap_probe(&device, &first) || !first.formatted || first.reopened_existing_file) return false;
    struct aurora_fs_bootstrap_result second = { 0 };
    if (!aurora_fs_bootstrap_probe(&device, &second) || second.formatted || !second.reopened_existing_file || second.generation <= first.generation) return false;
    struct aurora_partition partition = { 0 };
    partition.device = &device; partition.scheme = AURORA_PARTITION_SCHEME_WHOLE_DEVICE; partition.index = 0u; partition.first_lba = 0u; partition.block_count = device.block_count;
    const struct aurora_fs_driver *driver = aurora_fs_driver();
    if (driver == NULL || driver->probe == NULL || driver->mount == NULL || driver->stat == NULL || driver->read == NULL) return false;
    if (driver->probe(&partition) != AURORA_FS_PROBE_MATCH_READ_WRITE) return false;
    void *context = NULL;
    if (!driver->mount(&partition, &context) || context == NULL) return false;
    struct aurora_fs_stat stat;
    if (!driver->stat(context, "/aurora.boot-probe", &stat) || stat.type != AURORA_FS_ENTRY_FILE || stat.size != 17u) return false;
    static const uint8_t expected[] = "AURORA-FS-PERSIST";
    uint8_t buffer[sizeof(expected) - 1u]; size_t read = 0u;
    if (!driver->read(context, "/aurora.boot-probe", 0u, buffer, sizeof(buffer), &read) || read != sizeof(buffer) || !bytes_equal(buffer, expected, sizeof(buffer))) return false;
    if (driver->unmount != NULL) driver->unmount(context);

    if (!aurora_fs_v2_layout_self_test()) return false;
    log_line("[aurorafs-v2] 4KiB layout + bitmap + 64-bit inode + multi-block extent self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_directory_self_test()) return false;
    log_line("[aurorafs-v2] dynamic two-block root + nested directory traversal self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_allocator_self_test()) return false;
    log_line("[aurorafs-v2] multi-block bitmap + cross-boundary range allocator self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_formatter_self_test()) return false;
    log_line("[aurorafs-v2] scalable multi-bitmap formatter + reopen/allocator integration self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_extent_tree_self_test()) return false;
    log_line("[aurorafs-v2] two-level extent tree + 130 fragmented extents persistence self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_inode_extent_self_test()) return false;
    log_line("[aurorafs-v2] persistent inode inline-to-tree promotion at fifth extent self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_inode_tree_append_self_test()) return false;
    log_line("[aurorafs-v2] tree-backed inode copy-on-write sixth-extent append self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_extent_tree_growth_self_test()) return false;
    log_line("[aurorafs-v2] full-leaf COW 126-to-127 extent growth into level-1 root self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_inode_tree_growth_self_test()) return false;
    log_line("[aurorafs-v2] persistent inode COW publication of 127th extent through level-1 root self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_extent_tree_level1_append_self_test()) return false;
    log_line("[aurorafs-v2] existing level-1 root COW append with last-leaf replacement self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_inode_level1_append_self_test()) return false;
    log_line("[aurorafs-v2] persistent inode level-1 COW publication of 128th extent self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_extent_tree_level1_full_leaf_self_test()) return false;
    log_line("[aurorafs-v2] level-1 full-last-leaf COW append with new child leaf self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_inode_level1_full_leaf_self_test()) return false;
    log_line("[aurorafs-v2] persistent inode level-1 COW publication of 253rd extent self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_extent_tree_level2_growth_self_test()) return false;
    log_line("[aurorafs-v2] full level-1 root COW growth to level-2 at 15877th extent self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_inode_level2_growth_self_test()) return false;
    log_line("[aurorafs-v2] persistent inode publication of 15877th extent through level-2 root self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_unified_lookup_self_test()) return false;
    log_line("[aurorafs-v2] unified leaf/level-1/level-2 tree and inode lookup self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_level3_lookup_self_test()) return false;
    log_line("[aurorafs-v2] bounded unified level-3 tree + inode lookup self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_extent_tree_level3_growth_self_test()) return false;
    log_line("[aurorafs-v2] full level-2 root COW growth to level-3 at 2000377th extent self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_level3_commit_self_test()) return false;
    log_line("[aurorafs-v2] persistent full level-2 to level-3 growth + inode publication + reopen lookup self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_extent_tree_level3_append_self_test()) return false;
    log_line("[aurorafs-v2] existing level-3 root COW append through final leaf/level-1/level-2/root replacement self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_extent_tree_level2_append_self_test()) return false;
    log_line("[aurorafs-v2] existing level-2 root COW append through leaf/level-1/root replacement self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_inode_publish_extent_root_self_test()) return false;
    log_line("[aurorafs-v2] durable inode COW root publication + reopen self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_level2_commit_self_test()) return false;
    log_line("[aurorafs-v2] end-to-end level-2 COW append + inode publication + reopen lookup self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_extent_tree_level2_full_leaf_self_test()) return false;
    log_line("[aurorafs-v2] level-2 full-last-leaf COW append with new child leaf self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_level2_full_leaf_commit_self_test()) return false;
    log_line("[aurorafs-v2] persistent level-2 full-last-leaf COW append + inode publication + reopen lookup self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_extent_tree_level2_full_level1_self_test()) return false;
    log_line("[aurorafs-v2] level-2 full level-1 sibling COW growth self-test passed on 512/4096-byte devices");
    if (!aurora_fs_v2_level2_full_level1_commit_self_test()) return false;
    log_line("[aurorafs-v2] persistent level-2 full-level1 sibling growth + inode publication + reopen lookup self-test passed on 512/4096-byte devices");
    return true;
}
