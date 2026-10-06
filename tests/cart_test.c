#include "tic80_mister/cart.h"
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed line %d\n", __LINE__); return 1; } } while (0)
int main(void)
{
    const uint8_t valid[] = {5, 3, 0, 0, 'a', 'b', 'c', 17, 0, 0, 0};
    const uint8_t duplicate[] = {5, 1, 0, 0, 'a', 5, 1, 0, 0, 'b'};
    const uint8_t truncated[] = {5, 0, 0, 0};
    const uint8_t invalid_bank[] = {19 | (4 << 5), 1, 0, 0, 1};
    const uint8_t empty_palette[] = {12, 0, 0, 0};
    const uint8_t defaults[] = {17, 0, 0, 0};
    CHECK(tm_cart_validate(valid, sizeof valid) == 0);
    CHECK(tm_cart_validate(valid, sizeof valid - 1) == -1);
    CHECK(tm_cart_validate(valid, 6) == -1);
    CHECK(tm_cart_validate(NULL, 0) == -1);
    CHECK(tm_cart_validate(duplicate, sizeof duplicate) == -1);
    CHECK(tm_cart_validate(truncated, sizeof truncated) == -1);
    CHECK(tm_cart_validate(invalid_bank, sizeof invalid_bank) == -1);
    CHECK(tm_cart_validate(empty_palette, sizeof empty_palette) == -1);
    CHECK(tm_cart_validate(defaults, sizeof defaults) == 0);
    const uint8_t png_prefix[]={137,'P','N','G',0,0,0,0};
    CHECK(tm_cart_validate(png_prefix,sizeof png_prefix)==-1);
    return 0;
}
