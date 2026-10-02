#ifndef AURORA_BOOTSTRAP_PROBE_H
#define AURORA_BOOTSTRAP_PROBE_H

struct aurora_block_device;

void bootstrap_storage_probe(void);
void bootstrap_storage_probe_device(
    struct aurora_block_device *device,
    const char *transport
);

#endif
