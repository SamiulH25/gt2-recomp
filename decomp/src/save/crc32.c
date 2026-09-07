// GT2 native port: CRC-32. Matches SCUS gt2_save_crc32 (0x80083178):
// crc = 0xFFFFFFFF; per byte crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
// return ~crc. Table generated for the standard polynomial so the
// binary carries no 1 KB copy of the disc's table.

#include "gt2/save.h"

static u32 crc_table[256];
static int table_ready = 0;

static void build_table(void) {
    for (u32 i = 0; i < 256; i++) {
        u32 c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    table_ready = 1;
}

u32 gt2_crc32(const u8 *data, u32 len) {
    if (!table_ready)
        build_table();
    u32 crc = 0xFFFFFFFFu;
    for (u32 i = 0; i < len; i++)
        crc = crc_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}
