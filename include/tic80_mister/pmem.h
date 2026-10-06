#ifndef TIC80_MISTER_PMEM_H
#define TIC80_MISTER_PMEM_H
#include "tic80.h"
#include <stdint.h>
typedef struct {
    uint32_t saved[256];
    char *path;
    struct tm_pmem_worker *worker;
} tm_pmem;
/* Load after tic80_load, before the first tick. Missing files are normal;
 * malformed files are errors and are never silently replaced. */
int tm_pmem_open(tm_pmem *save, tic80 *tic, const char *path);
int tm_pmem_save(tm_pmem *save, tic80 *tic);
/* Queue a private snapshot; filesystem I/O runs on a worker. Repeated pending
 * snapshots coalesce. A previous write failure returns -1 while queuing a retry
 * of the latest snapshot; successful retry clears it. tm_pmem_save joins/drains
 * the worker before a final synchronous save. */
int tm_pmem_schedule(tm_pmem *save, tic80 *tic);
/* Snapshot interface for supervisors that own acknowledged persistent memory
 * rather than a runtime pointer. The on-disk format and writer are shared with
 * the runtime interface. Open changes values only after full validation;
 * schedule copies them before returning. */
int tm_pmem_open_values(tm_pmem *save, uint32_t values[256], const char *path);
int tm_pmem_save_values(tm_pmem *save, const uint32_t values[256]);
int tm_pmem_schedule_values(tm_pmem *save, const uint32_t values[256]);
/* No filesystem I/O: -1 reports the last background failure, 1 reports queued
 * or active work, and 0 reports no outstanding work. Call from the context's
 * owner, without concurrently opening, closing, or synchronously saving it. */
int tm_pmem_status(tm_pmem *save);
/* Upstream save identity: explicit saveid metadata, otherwise bank0 digest. */
void tm_pmem_key(tic80 *tic, char key[33]);
void tm_pmem_close(tm_pmem *save);
#endif
