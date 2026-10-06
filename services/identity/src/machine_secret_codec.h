#ifndef AURORA_IDENTITY_MACHINE_SECRET_CODEC_H
#define AURORA_IDENTITY_MACHINE_SECRET_CODEC_H

#include "aurora/identity/machine_secret.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AURORA_IDENTITY_MACHINE_SECRET_DISK_SIZE 84u

bool aurora_identity_machine_secret_record_encode(
    const struct aurora_identity_machine_secret_record *record,
    uint8_t out[AURORA_IDENTITY_MACHINE_SECRET_DISK_SIZE]);

bool aurora_identity_machine_secret_record_decode(
    const uint8_t *input,
    size_t input_size,
    struct aurora_identity_machine_secret_record *out_record);

#endif
