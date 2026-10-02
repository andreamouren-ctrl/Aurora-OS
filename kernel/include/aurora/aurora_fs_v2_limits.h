#ifndef AURORA_AURORA_FS_V2_LIMITS_H
#define AURORA_AURORA_FS_V2_LIMITS_H

#include <stdint.h>

#define AURORA_FS_V2_EXTENT_NODE_CAPACITY 126u
#define AURORA_FS_V2_MAX_ROOT_LEVEL 3u
#define AURORA_FS_V2_LEVEL1_EXTENT_CAPACITY 15876ull
#define AURORA_FS_V2_LEVEL2_EXTENT_CAPACITY 2000376ull
#define AURORA_FS_V2_LEVEL3_EXTENT_CAPACITY 252047376ull

_Static_assert(
    AURORA_FS_V2_LEVEL1_EXTENT_CAPACITY ==
        (uint64_t)AURORA_FS_V2_EXTENT_NODE_CAPACITY *
        (uint64_t)AURORA_FS_V2_EXTENT_NODE_CAPACITY,
    "AuroraFS v2 level-1 capacity contract changed"
);

_Static_assert(
    AURORA_FS_V2_LEVEL2_EXTENT_CAPACITY ==
        AURORA_FS_V2_LEVEL1_EXTENT_CAPACITY *
        (uint64_t)AURORA_FS_V2_EXTENT_NODE_CAPACITY,
    "AuroraFS v2 level-2 capacity contract changed"
);

_Static_assert(
    AURORA_FS_V2_LEVEL3_EXTENT_CAPACITY ==
        AURORA_FS_V2_LEVEL2_EXTENT_CAPACITY *
        (uint64_t)AURORA_FS_V2_EXTENT_NODE_CAPACITY,
    "AuroraFS v2 level-3 capacity contract changed"
);

#endif
