#ifndef AURORA_EXFAT_H
#define AURORA_EXFAT_H

#include <stdbool.h>

#include <aurora/fs_driver.h>

const struct aurora_fs_driver *exfat_driver(void);
bool exfat_4kn_self_test(void);

#endif
