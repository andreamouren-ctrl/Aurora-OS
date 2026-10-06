#ifndef AURORA_PROCESS_LIFECYCLE_PROBE_H
#define AURORA_PROCESS_LIFECYCLE_PROBE_H

#include <stdbool.h>

/*
 * Runtime lifecycle proof: repeatedly creates, exits, reaps, and releases
 * isolated Ring 3 processes beyond the scheduler's fixed slot capacity.
 */
bool process_lifecycle_self_test(void);

#endif
