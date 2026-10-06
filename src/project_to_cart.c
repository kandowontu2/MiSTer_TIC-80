#include "studio/project.h"
#include "tic80_mister/cart.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "Usage: %s source_project output.tic\n", argv[0]); return 2; }
    FILE *source = fopen(argv[1], "rb");
    if (!source) { perror(argv[1]); return 1; }
    if (fseek(source, 0, SEEK_END)) { fclose(source); return 1; }
    long size = ftell(source);
    if (size < 1 || size >= TIC_CODE_SIZE) { fclose(source); return 1; }
    rewind(source);
    char *text = malloc((size_t)size + 1);
    tic_cartridge *cart = calloc(1, sizeof *cart);
    u8 *bytes = malloc(sizeof *cart * 2);
    int failed = !text || !cart || !bytes;
    if (!failed && fread(text, 1, size, source) != (size_t)size) failed = 1;
    fclose(source);
    if (!failed) {
        text[size] = 0;
        if (!tic_project_load(argv[1], text, (s32)size, cart)) failed = 1;
    }
    if (!failed) {
        s32 length = tic_cart_save(cart, bytes);
        if (length <= 0 || tm_cart_validate(bytes, length)) failed = 1;
        else {
            FILE *output = fopen(argv[2], "wb");
            if (!output) { perror(argv[2]); failed = 1; }
            else {
                if (fwrite(bytes, 1, length, output) != (size_t)length) failed = 1;
                if (fclose(output)) failed = 1;
            }
        }
    }
    free(bytes); free(cart); free(text);
    return failed ? 1 : 0;
}
