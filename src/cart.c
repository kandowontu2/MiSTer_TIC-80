#include "tic80_mister/cart.h"

int tm_cart_validate(const uint8_t *bytes, size_t size)
{
    if (!bytes || size < 4 || size > 4 * 1024 * 1024) return -1;
    /* Upstream detects PNG using these four bytes. Never pass a malformed
     * PNG or nested PNG payload through its unchecked native-load path. */
    if (bytes[0]==137 && bytes[1]=='P' && bytes[2]=='N' && bytes[3]=='G') return -1;
    size_t offset = 0, code = 0, binary = 0;
    unsigned code_banks = 0, binary_banks = 0;
    while (offset < size) {
        if (size - offset < 4) return -1;
        unsigned type = bytes[offset] & 31;
        unsigned bank = bytes[offset] >> 5;
        size_t payload = bytes[offset + 1] | ((size_t)bytes[offset + 2] << 8);
        offset += 4;
        if (!payload && (type == 5 || type == 19)) payload = 65536;
        if (payload > size - offset) return -1;
        /* Upstream LOAD_CHUNK interprets a zero size as a 64K copy even for
         * non-code chunks whose traversal length is zero. Such an envelope
         * would read past its payload. DEFAULT's empty chunk is legitimate. */
        if (!payload && (type == 1 || type == 2 || type == 3 || type == 4 ||
                         type == 6 || type == 9 || type == 10 || type == 12 ||
                         type == 13 || type == 14 || type == 15 || type == 16 ||
                         type == 18 || type == 20)) return -1;
        if (type == 5) {
            if (code_banks & (1u << bank)) return -1;
            code_banks |= 1u << bank;
            code += payload;
            /* Upstream code buffer is 8 banks including its final NUL byte. */
            if (code >= 8 * 65536) return -1;
        }
        if (type == 19) {
            if (bank >= 4) return -1;
            if (binary_banks & (1u << bank)) return -1;
            binary_banks |= 1u << bank;
            binary += payload;
            if (binary > 4 * 65536) return -1;
        }
        offset += payload;
    }
    return 0;
}
