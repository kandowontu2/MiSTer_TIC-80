/* Negative control: bypass only microphone handoff in the actual service. */
#define _GNU_SOURCE
#include "tic80_mister/vm.h"
static int skip_capture(tm_vm* vm) { (void)vm; return TM_VM_OK; }
#define tm_vm_fft_pause skip_capture
#define tm_vm_fft_resume skip_capture
#include TM_SERVE_SOURCE
