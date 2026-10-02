#ifndef AURORA_NVME_H
#define AURORA_NVME_H

#include <stdbool.h>
#include <stdint.h>

struct aurora_nvme_probe_result {
    bool found;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint64_t bar0_physical;
    uint64_t capabilities;
    uint32_t version;
    uint32_t controller_status;
    uint16_t max_queue_entries;
    uint8_t doorbell_stride;
    uint8_t minimum_page_shift;
    uint8_t maximum_page_shift;
};

struct aurora_nvme_admin_result {
    char model[41];
    char serial[21];
    uint32_t namespace_count;
    uint32_t namespace_id;
    uint64_t block_count;
    uint32_t block_size;
};

bool nvme_probe(struct aurora_nvme_probe_result *out_result);
bool nvme_admin_identify(struct aurora_nvme_admin_result *out_result);
void nvme_bootstrap_probe(void);

#endif
