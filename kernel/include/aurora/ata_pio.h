#ifndef AURORA_ATA_PIO_H
#define AURORA_ATA_PIO_H

#include <stdbool.h>

#include <aurora/block_device.h>

bool ata_pio_primary_master_init(void);
struct aurora_block_device *ata_pio_primary_master_device(void);
bool ata_pio_ci_probe(void);

#endif
