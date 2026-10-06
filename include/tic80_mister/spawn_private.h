#ifndef TIC80_MISTER_SPAWN_PRIVATE_H
#define TIC80_MISTER_SPAWN_PRIVATE_H
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct { int source, target; } tm_spawn_fd;
typedef struct {
    const char *file;
    char *const *argv;
    char *const *envp;
    const tm_spawn_fd *fds;
    size_t count;
    int close_from;
    bool search_path, null_input, null_error, process_group;
} tm_spawn_request;

/* Sources must be reserved above close_from; targets must be below it.
 * Return an errno value, as posix_spawn does. Publish pid only after exec. */
int tm_spawn_closed(pid_t *pid, const tm_spawn_request *request);
#endif
