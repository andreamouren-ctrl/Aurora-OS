#ifndef AURORA_FAT32_H
#define AURORA_FAT32_H

#include <aurora/fs_driver.h>

const struct aurora_fs_driver *fat32_driver(void);
bool fat32_4kn_self_test(void);

#endif
