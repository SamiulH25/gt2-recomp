// gt2_cd host tests: raw 2352 dump and cooked ISO must read identically.
// GT2_ISO (default /tmp/opencode/gt2.iso), GT2_RAW_BIN (default: the repo's
// legal dump). SKIP (exit 0) when an image is absent.

#include "gt2/cd.h"

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

#define RAW_DEFAULT \
    "Gran Turismo 2 (USA) (Simulation Mode) (v1.2)/" \
    "Gran Turismo 2 (USA) (Simulation Mode) (v1.2).bin"

int main(void) {
    const char *iso = getenv("GT2_ISO");
    if (!iso || !iso[0])
        iso = "/tmp/opencode/gt2.iso";
    const char *raw = getenv("GT2_RAW_BIN");
    if (!raw || !raw[0])
        raw = RAW_DEFAULT;

    gt2_cd_t *c = NULL, *r = NULL;
    gt2_cd_status_t st = gt2_cd_open(iso, &c);
    if (st == GT2_CD_ERR_IO) {
        printf("SKIP: cannot open '%s'\n", iso);
        return 0;
    }
    CHECK(st == GT2_CD_OK, "open cooked: %s", gt2_cd_strerror(st));
    st = gt2_cd_open(raw, &r);
    if (st == GT2_CD_ERR_IO) {
        printf("SKIP: cannot open '%s'\n", raw);
        gt2_cd_close(c);
        return 0;
    }
    CHECK(st == GT2_CD_OK, "open raw: %s", gt2_cd_strerror(st));
    if (!c || !r)
        return 1;
    CHECK(!gt2_cd_is_raw(c), "cooked detected as raw");
    CHECK(gt2_cd_is_raw(r), "raw not detected");

    // VOL header sector agrees, magic GTFS.
    u8 s1[GT2_SECTOR_SIZE], s2[GT2_SECTOR_SIZE];
    CHECK(gt2_cd_read_sector(c, GT2_VOL_LBA, s1) == GT2_CD_OK, "cooked sec");
    CHECK(gt2_cd_read_sector(r, GT2_VOL_LBA, s2) == GT2_CD_OK, "raw sec");
    CHECK(!memcmp(s1, s2, GT2_SECTOR_SIZE), "sector 473 differs");
    CHECK(!memcmp(s1, "GTFS", 4), "no GTFS magic");

    // Spanning pread across a sector boundary, both images.
    u8 b1[3000], b2[3000];
    u32 off = GT2_VOL_BASE + 2000;
    CHECK(gt2_cd_pread(c, off, b1, sizeof b1) == GT2_CD_OK, "cooked pread");
    CHECK(gt2_cd_pread(r, off, b2, sizeof b2) == GT2_CD_OK, "raw pread");
    CHECK(!memcmp(b1, b2, sizeof b1), "pread differs");

    // Known content: tbl[36] region (arc_topmenu, gzip head 1f8b).
    u8 h1[4], h2[4];
    u32 aoff = GT2_VOL_BASE + 0x341800;
    CHECK(gt2_cd_pread(c, aoff, h1, 4) == GT2_CD_OK, "cooked head");
    CHECK(gt2_cd_pread(r, aoff, h2, 4) == GT2_CD_OK, "raw head");
    CHECK(h1[0] == 0x1F && h1[1] == 0x8B, "arc head %02x%02x", h1[0], h1[1]);
    CHECK(!memcmp(h1, h2, 4), "raw head differs");

    // Errors.
    CHECK(gt2_cd_read_sector(NULL, 0, s1) == GT2_CD_ERR_INVAL, "null cd");
    CHECK(gt2_cd_read_sector(c, 0xFFFFFFFu, s1) == GT2_CD_ERR_IO,
          "past-end must be IO");

    gt2_cd_close(c);
    gt2_cd_close(r);
    gt2_cd_close(NULL);

    if (failures == 0)
        printf("PASS test_cd (raw+cooked agree)\n");
    else
        printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
