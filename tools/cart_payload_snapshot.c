#define _POSIX_C_SOURCE 200809L
/* Read-only verification of the bytes delivered by stock Main. No session ownership. */
#include "tic80_mister/memory_map.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int selected(void)
{
    char name[80] = {0};
    FILE *file = fopen("/tmp/CORENAME", "r");
    if (!file) return 0;
    int read = fgets(name, sizeof name, file) != NULL;
    fclose(file);
    name[strcspn(name, "\r\n")] = 0;
    return read && !strcmp(name, "TIC-80");
}

static uint32_t reg(const volatile uint8_t *map, unsigned offset)
{
    uint32_t value = *(const volatile uint32_t *)(map + offset);
    __sync_synchronize();
    return value;
}

static int coherent(const volatile uint8_t *map, uint32_t ticket, uint32_t size,
                    uint32_t session)
{
    return reg(map, TM_IDENTITY_OFFSET) == TM_MAGIC &&
           reg(map, TM_CART_META_OFFSET) == ticket &&
           reg(map, TM_CART_META_OFFSET + 4) == size &&
           reg(map, TM_CART_ACK_OFFSET) == ticket &&
           reg(map, TM_CART_SOURCE_OFFSET + 4) == 0 &&
           reg(map, TM_SESSION_REQUEST_OFFSET) == session &&
           reg(map, TM_SESSION_ACK_OFFSET) == session;
}

static int number(const char *text, uint32_t *result)
{
    char *end;
    errno = 0;
    unsigned long value = strtoul(text, &end, 0);
    if (errno || !*text || *end || value > UINT32_MAX || text[0] == '-') return 0;
    *result = (uint32_t)value;
    return 1;
}

int main(int argc, char **argv)
{
    const char *memory = NULL;
    int first = 1;
    if (argc == 6 && !strcmp(argv[1], "--memory")) {
        memory = argv[2]; first = 3;
    } else if (argc != 4) {
        fprintf(stderr, "Usage: %s [--memory fixture-file] ticket bytes output-file\n", argv[0]);
        return 2;
    }
    uint32_t ticket, size;
    if (!number(argv[first], &ticket) || (ticket & 3) != 2 ||
        !number(argv[first + 1], &size) || !size || size > TM_CART_CAPACITY ||
        (!memory && !selected())) return 2;
    int fd = open(memory ? memory : "/dev/mem", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 1;
    struct stat st;
    if (memory && (fstat(fd, &st) || !S_ISREG(st.st_mode) ||
                   st.st_size < (off_t)TM_REGION_BYTES)) { close(fd); return 1; }
    volatile uint8_t *map = mmap(NULL, TM_REGION_BYTES, PROT_READ, MAP_SHARED,
                                 fd, memory ? 0 : TM_PHYSICAL_BASE);
    close(fd);
    if (map == MAP_FAILED) return 1;
    uint32_t session = reg(map, TM_SESSION_ACK_OFFSET);
    uint8_t *data = malloc(size);
    int result = 1;
    if (!data || !session || !coherent(map, ticket, size, session)) goto done;
    memcpy(data, (const void *)(map + TM_CART_DATA_OFFSET), size);
    __sync_synchronize();
    if (!coherent(map, ticket, size, session) || (!memory && !selected())) goto done;
    fd = open(argv[first + 2], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) goto done;
    size_t written = 0;
    while (written < size) {
        ssize_t count = write(fd, data + written, size - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(fd); goto done; }
        written += (size_t)count;
    }
    if (close(fd)) goto done;
    printf("{\"ticket\":%u,\"bytes\":%u,\"session\":%u,\"read_only\":true}\n",
           ticket, size, session);
    result = 0;
done:
    free(data);
    munmap((void *)map, TM_REGION_BYTES);
    return result;
}
