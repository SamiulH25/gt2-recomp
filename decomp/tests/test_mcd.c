// gt2_mcd host tests: real formatted card + synthetic save round-trip.
// GT2_MCD env or saves/card1.mcd (SKIP if absent). The synthetic card
// is built in-memory per the spec (dir + SC title frame + payload).

#include "gt2/mcd.h"
#include "gt2/save.h"

#include <stdio.h>
#include <stdlib.h>
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

static u8 xorsum(const u8 *p, u32 n) {
    u8 x = 0;
    for (u32 i = 0; i < n; i++)
        x ^= p[i];
    return x;
}

// Build a 1-block GT2 save card in memory.
static void build_save(u8 *img, const u8 *payload, u32 plen) {
    memset(img, 0xFF, GT2_MCD_SIZE);
    // header
    img[0] = 'M';
    img[1] = 'C';
    img[0x7F] = xorsum(img, 0x7F);
    // dir[0]: first block, 8 KB, name BASCUS-94488 + filler
    u8 *d = img + 128;
    memset(d, 0, 128);
    d[0] = 0x51;
    d[4] = 0x00;
    d[5] = 0x20;    // size 0x2000
    d[8] = 0xFF;
    d[9] = 0xFF;    // next = end
    const char *nm = "BASCUS-94488GT2TEST!";
    memcpy(d + 10, nm, strlen(nm) + 1);
    d[0x7F] = xorsum(d, 0x7F);
    for (u32 i = 1; i < 15; i++) {
        u8 *e = img + (1 + i) * 128;
        memset(e, 0, 128);
        e[0] = 0xA0;
        e[8] = 0xFF;
        e[9] = 0xFF;
        e[0x7F] = xorsum(e, 0x7F);
    }
    // block 1: SC title frame + payload + crc32 tail
    u8 *b = img + 8192;
    memset(b, 0, 8192);
    b[0] = 'S';
    b[1] = 'C';
    b[2] = 0x11;    // 1 icon frame
    b[3] = 0x01;    // block count
    memcpy(b + 4, "GT2 TEST SAVE", 13);
    memcpy(b + 128, payload, plen);
    u32 crc = gt2_crc32(payload, plen);
    memcpy(b + 128 + plen, &crc, 4);
}

int main(void) {
    // 1. synthetic save round-trip (self-contained)
    static u8 img[GT2_MCD_SIZE];
    u8 payload[1024];
    for (int i = 0; i < 1024; i++)
        payload[i] = (u8)(i * 7 + 3);
    build_save(img, payload, sizeof payload);
    gt2_mcd_t *m = NULL;
    CHECK(gt2_mcd_open_mem(img, sizeof img, &m) == GT2_MCD_OK, "syn open");
    if (m) {
        CHECK(gt2_mcd_header_ok(m), "syn header");
        CHECK(gt2_mcd_file_count(m) == 1, "syn count=%u",
              gt2_mcd_file_count(m));
        gt2_mcd_dir_t e;
        CHECK(gt2_mcd_dir(m, 0, &e) == GT2_MCD_OK &&
              e.state == 0x51 && e.size == 0x2000 && e.checksum_ok &&
              !strcmp(e.name, "BASCUS-94488GT2TEST!"), "syn dir %s",
              e.name);
        u8 *data = NULL;
        u32 n = 0;
        CHECK(gt2_mcd_read(m, 0, &data, &n) == GT2_MCD_OK && n == 0x2000,
              "syn read n=%u", n);
        if (data) {
            CHECK(data[0] == 'S' && data[1] == 'C', "syn title magic");
            CHECK(!memcmp(data + 128, payload, sizeof payload),
                  "syn payload");
            u32 stored;
            memcpy(&stored, data + 128 + sizeof payload, 4);
            CHECK(stored == gt2_crc32(payload, sizeof payload),
                  "syn crc %08x", stored);
            free(data);
        }
        CHECK(gt2_mcd_read(m, 1, &data, &n) == GT2_MCD_ERR_NOT_FOUND,
              "free entry read");
        gt2_mcd_close(m);
    }
    CHECK(gt2_mcd_open_mem(img, 100, &m) == GT2_MCD_ERR_INVAL, "short img");
    CHECK(gt2_mcd_open_mem(NULL, GT2_MCD_SIZE, &m) == GT2_MCD_ERR_INVAL,
          "null img");

    // 2. real formatted card from the game (empty).
    const char *path = getenv("GT2_MCD");
    if (!path || !path[0])
        path = "saves/card1.mcd";
    gt2_mcd_t *c = NULL;
    if (gt2_mcd_open(path, &c) != GT2_MCD_OK) {
        printf("SKIP: cannot open '%s'\n", path);
    } else {
        CHECK(gt2_mcd_header_ok(c), "card header");
        CHECK(gt2_mcd_file_count(c) == 0, "card files=%u",
              gt2_mcd_file_count(c));
        gt2_mcd_dir_t e;
        CHECK(gt2_mcd_dir(c, 0, &e) == GT2_MCD_OK &&
              e.state == 0xA0 && e.checksum_ok, "card dir0 state=%#x",
              e.state);
        CHECK(gt2_mcd_dir(c, 15, &e) == GT2_MCD_ERR_INVAL, "dir oob");
        gt2_mcd_close(c);
    }
    gt2_mcd_close(NULL);

    // 3. write path on a formatted image.
    static u8 fresh[GT2_MCD_SIZE];
    CHECK(gt2_mcd_format(fresh) == GT2_MCD_OK, "format");
    CHECK(gt2_mcd_format(NULL) == GT2_MCD_ERR_INVAL, "format null");
    FILE *rf = fopen(path, "rb");
    if (rf) {
        // Byte-identical to the game-formatted card.
        static u8 real[GT2_MCD_SIZE];
        size_t rn = fread(real, 1, sizeof real, rf);
        fclose(rf);
        CHECK(rn == sizeof real && !memcmp(fresh, real, sizeof real),
              "format matches game card");
    } else {
        printf("SKIP: cannot open '%s'\n", path);
    }
    gt2_mcd_t *w = NULL;
    CHECK(gt2_mcd_open_mem(fresh, sizeof fresh, &w) == GT2_MCD_OK,
          "open fresh");
    if (w) {
        static u8 one[8192];
        for (int i = 0; i < 8192; i++)
            one[i] = (u8)(i * 13 + 7);
        CHECK(gt2_mcd_write(w, "BASCUS-94488GT2TEST!", one, sizeof one) ==
              GT2_MCD_OK, "write 1blk");
        CHECK(gt2_mcd_file_count(w) == 1, "count=%u",
              gt2_mcd_file_count(w));
        CHECK(gt2_mcd_write(w, "BASCUS-94488GT2TEST!", one, sizeof one) ==
              GT2_MCD_ERR_EXISTS, "dup");
        CHECK(gt2_mcd_write(w, "X", one, 100) == GT2_MCD_ERR_INVAL,
              "unbalanced size");
        u8 *rd = NULL;
        u32 rn2 = 0;
        CHECK(gt2_mcd_read(w, 0, &rd, &rn2) == GT2_MCD_OK &&
              rn2 == sizeof one && !memcmp(rd, one, sizeof one),
              "read back");
        free(rd);
        // 2-block chained file.
        static u8 two[16384];
        for (int i = 0; i < 16384; i++)
            two[i] = (u8)(i * 29 + 11);
        CHECK(gt2_mcd_write(w, "BASCUS-94488GT2BIG!", two, sizeof two) ==
              GT2_MCD_OK, "write 2blk");
        CHECK(gt2_mcd_file_count(w) == 3, "count2=%u",
              gt2_mcd_file_count(w));
        rd = NULL;
        CHECK(gt2_mcd_read(w, 1, &rd, &rn2) == GT2_MCD_OK &&
              rn2 == sizeof two && !memcmp(rd, two, sizeof two),
              "read chained");
        free(rd);
        // delete + reuse.
        CHECK(gt2_mcd_delete(w, 0) == GT2_MCD_OK, "delete");
        CHECK(gt2_mcd_file_count(w) == 2, "count3=%u",
              gt2_mcd_file_count(w));
        CHECK(gt2_mcd_read(w, 0, &rd, &rn2) == GT2_MCD_ERR_NOT_FOUND,
              "read deleted");
        CHECK(gt2_mcd_write(w, "BASCUS-94488GT2TEST!", one, sizeof one) ==
              GT2_MCD_OK, "rewrite after delete");
        CHECK(gt2_mcd_delete(w, 9) == GT2_MCD_ERR_NOT_FOUND, "delete free");
        // image bytes are self-consistent (all dir checksums valid).
        const u8 *bytes = gt2_mcd_bytes(w);
        CHECK(bytes != NULL, "bytes");
        int badck = 0;
        for (u32 i = 0; i < 15; i++) {
            const u8 *f = bytes + (1 + i) * 128;
            u8 x = 0;
            for (u32 k = 0; k < 0x7F; k++)
                x ^= f[k];
            if (x != f[0x7F])
                badck++;
        }
        CHECK(badck == 0, "dir checksums bad=%d", badck);
        gt2_mcd_close(w);
    }

    if (failures == 0)
        printf("PASS test_mcd (synthetic + formatted card ok)\n");
    else
        printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
