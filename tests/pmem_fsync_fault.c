#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Service integration fixture: fail one save-file fsync inside a named
 * temporary test directory. All unrelated filesystem operations pass through. */
int fsync(int fd)
{
    static int injected;
    int (*original)(int) = dlsym(RTLD_NEXT, "fsync");
    if (!original) { errno = EIO; return -1; }
    const char *prefix = getenv("TM_TEST_PMEM_FAIL_ONCE_DIR");
    if (prefix && *prefix) {
        char descriptor[64], path[1024];
        snprintf(descriptor, sizeof descriptor, "/proc/self/fd/%d", fd);
        ssize_t length = readlink(descriptor, path, sizeof path - 1);
        if (length >= 0) {
            path[length] = 0;
            if (!strncmp(path, prefix, strlen(prefix)) && strstr(path, ".pmem.tmp-") &&
                    __sync_bool_compare_and_swap(&injected, 0, 1)) {
                fputs("Injected one persistent-memory fsync failure\n", stderr);
                errno = EIO;
                return -1;
            }
        }
    }
    return original(fd);
}
