/* Internal pinned TIC-80 history layout; derived from its MIT-licensed
 * src/ext/history.c. The staged implementation includes this same definition. */
#ifndef TIC80_MISTER_HISTORY_PRIVATE_H
#define TIC80_MISTER_HISTORY_PRIVATE_H
#include "ext/history.h"
#include <stdint.h>
typedef struct { u8 *buffer; u32 start,end; } tm_history_data;
typedef struct tm_history_item tm_history_item;
struct tm_history_item { tm_history_item *next,*prev; tm_history_data data; };
struct History { tm_history_item *list; u32 size; u8 *state; void *data; uint64_t revision; };
uint64_t tm_history_revision(const History *history);
void tm_history_touch(History *history);
int tm_history_snapshot(const History *history,u32 request);
bool tm_history_snapshot_valid(int descriptor,u32 size,u32 *request);
History *tm_history_snapshot_restore(void *data,u32 size,int descriptor);
enum { TM_HISTORY_GROUP_MAX=64 };
int tm_history_bundle_snapshot(History *const histories[],u32 count,u32 request);
bool tm_history_bundle_valid(int descriptor,const u32 sizes[],u32 count,u32 *request);
/* Parse every member before replacing any history or external data. Only
 * explicitly selected private editor buffers receive captured current data;
 * acknowledged cartridge assets stay intact until an actual Undo/Redo. */
bool tm_history_bundle_restore(History **const slots[],const bool restore_current[],u32 count,int descriptor);
#endif
