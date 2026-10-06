/* Test-only: make cartridge trace writes observable before a worker is killed.
 * This library is never part of the installed runtime or release payload. */
#include <stdio.h>
__attribute__((constructor)) static void unbuffered_stdout(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
}
