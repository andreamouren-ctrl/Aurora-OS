#include <stddef.h>
#include <stdint.h>

#include <aurora/pmm.h>
#include <aurora/process.h>
#include <aurora/usercopy.h>
#include <aurora/vmm.h>

#define USER_TOP_EXCLUSIVE 0x0000800000000000ull

static bool user_range_valid(
    uint64_t address,
    size_t length
) {
    if (length == 0) {
        return address <
            USER_TOP_EXCLUSIVE;
    }

    if (address >=
        USER_TOP_EXCLUSIVE) {
        return false;
    }

    uint64_t end =
        address +
        (uint64_t)length;

    if (end < address) {
        return false;
    }

    return end <=
        USER_TOP_EXCLUSIVE;
}

static size_t min_size(
    size_t left,
    size_t right
) {
    return left < right
        ? left
        : right;
}

static void copy_bytes(
    void *destination,
    const void *source,
    size_t length
) {
    uint8_t *dst = destination;
    const uint8_t *src = source;

    for (size_t i = 0;
         i < length;
         ++i) {
        dst[i] = src[i];
    }
}

static bool copy_user_common(
    struct aurora_process *process,
    uint64_t user_address,
    void *kernel_buffer,
    size_t length,
    bool write_user
) {
    if (process == NULL ||
        kernel_buffer == NULL ||
        !user_range_valid(
            user_address,
            length)) {
        return false;
    }

    size_t completed = 0;

    while (completed < length) {
        uint64_t current_user =
            user_address +
            completed;

        struct vmm_mapping_info mapping;

        if (!vmm_query_in(
                &process->address_space,
                current_user,
                &mapping)) {
            return false;
        }

        if ((mapping.flags &
             VMM_FLAG_USER) == 0) {
            return false;
        }

        if (write_user &&
            (mapping.flags &
             VMM_FLAG_WRITE) == 0) {
            return false;
        }

        uint64_t page_offset =
            current_user &
            (mapping.page_size - 1ull);

        size_t remaining_page =
            (size_t)(
                mapping.page_size -
                page_offset
            );

        size_t chunk =
            min_size(
                length - completed,
                remaining_page
            );

        void *physical_alias =
            pmm_phys_to_virt(
                mapping.physical_address
            );

        if (physical_alias == NULL) {
            return false;
        }

        if (write_user) {
            copy_bytes(
                physical_alias,
                (const uint8_t *)
                    kernel_buffer +
                    completed,
                chunk
            );
        } else {
            copy_bytes(
                (uint8_t *)
                    kernel_buffer +
                    completed,
                physical_alias,
                chunk
            );
        }

        completed += chunk;
    }

    return true;
}

bool copy_from_user(
    struct aurora_process *process,
    void *kernel_destination,
    uint64_t user_source,
    size_t length
) {
    if (length == 0) {
        return true;
    }

    return copy_user_common(
        process,
        user_source,
        kernel_destination,
        length,
        false
    );
}

bool copy_to_user(
    struct aurora_process *process,
    uint64_t user_destination,
    const void *kernel_source,
    size_t length
) {
    if (length == 0) {
        return true;
    }

    return copy_user_common(
        process,
        user_destination,
        (void *)kernel_source,
        length,
        true
    );
}
