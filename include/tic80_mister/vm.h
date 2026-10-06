#ifndef TIC80_MISTER_VM_H
#define TIC80_MISTER_VM_H
#include "tic80.h"
#include "tic80_mister/fft.h"
#include <stddef.h>
#include <stdint.h>
typedef struct tm_vm tm_vm;
enum { TM_VM_OK, TM_VM_ERROR, TM_VM_EXIT, TM_VM_TIMEOUT, TM_VM_DIED };
/* Linux live players execute untrusted cartridges in an exec'd child, with
 * bounded command waits. The executable must dispatch --vm-worker to entry().
 * Creation loads the cart and obtains its save identity, without a game tick. */
int tm_vm_open(tm_vm **out, const uint8_t *bytes, size_t size);
int tm_vm_open_configured(tm_vm **out, const uint8_t *bytes, size_t size, const tm_fft_config *capture);
/* Capture state from the acknowledged worker; default open disables
 * capture. The caller's configuration and device string are copied. */
int tm_vm_fft_status(const tm_vm *vm);
/* Release an exclusive microphone while validating a replacement cartridge;
 * resume the existing VM if that candidate is rejected. Its script/pmem remain
 * intact; resumed capture starts with fresh samples and gain/smoothing state. */
int tm_vm_fft_pause(tm_vm *vm);
int tm_vm_fft_resume(tm_vm *vm);
tic80 *tm_vm_product(tm_vm *vm);
const char *tm_vm_key(tm_vm *vm);
/* Product contains only completed frame/audio/pmem snapshots. Load pmem into
 * it before the first tick; subsequent VM state lives exclusively in the child.
 * offset excludes service pauses from the VM's monotonic nanosecond clock. */
int tm_vm_tick(tm_vm *vm, tic80_input input, uint64_t offset);
void tm_vm_close(tm_vm *vm);
int tm_vm_worker(int argc, char **argv);
#endif
