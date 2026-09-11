// gt2_save host tests: CRC-32 vectors (standard check + measured).

#include "gt2/save.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
        failures++; \
    } \
} while (0)

int main(void) {
    // Standard check vector (also produced by emulating 0x80083178).
    CHECK(gt2_crc32((const u8 *)"123456789", 9) == 0xCBF43926u,
          "check vector");
    CHECK(gt2_crc32(NULL, 0) == 0x00000000u, "empty");
    // Measured with zlib (binascii.crc32).
    CHECK(gt2_crc32((const u8 *)"GT2 overlay m0 test block", 25) ==
          0xB13503BAu, "block vector");
    CHECK(gt2_crc32((const u8 *)"arc_topmenu", 11) == 0xA59FBD4Du,
          "name vector");
    // Single byte + prefix property.
    CHECK(gt2_crc32((const u8 *)"a", 1) == 0xE8B7BE43u, "single");
    u8 buf[256];
    for (int i = 0; i < 256; i++)
        buf[i] = (u8)i;
    CHECK(gt2_crc32(buf, 256) == 0x29058C73u, "0..255");

    if (failures == 0)
        printf("PASS test_save (crc32 ok)\n");
    else
        printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
