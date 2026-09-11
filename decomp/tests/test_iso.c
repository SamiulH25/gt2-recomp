// gt2_iso host tests: ISO9660 root resolution on raw + cooked images.
// GT2_ISO / GT2_RAW_BIN as in test_cd.c. SKIP when an image is absent.

#include "gt2/cd.h"
#include "gt2/iso.h"

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

static void check_image(const char *path, const char *tag) {
    gt2_cd_t *cd = NULL;
    if (gt2_cd_open(path, &cd) != GT2_CD_OK) {
        printf("SKIP: cannot open '%s'\n", path);
        return;
    }
    gt2_iso_entry_t e;
    CHECK(gt2_iso_stat(cd, "GT2.OVL;1", &e) == GT2_ISO_OK, "%s ovl stat", tag);
    CHECK(e.extent_lba == 331 && e.size == 289752,
          "%s ovl lba=%u size=%u", tag, e.extent_lba, e.size);
    CHECK(gt2_iso_stat(cd, "/gt2.vol;1", &e) == GT2_ISO_OK, "%s vol stat",
          tag);
    CHECK(e.extent_lba == GT2_VOL_LBA && e.size == 488241152,
          "%s vol lba=%u size=%u", tag, e.extent_lba, e.size);
    // case-insensitive, no-subdir, missing, dir-as-file rules
    CHECK(gt2_iso_stat(cd, "gt2.ovl;1", &e) == GT2_ISO_OK, "%s ci", tag);
    CHECK(gt2_iso_stat(cd, "GT2.OVL", &e) == GT2_ISO_ERR_NOT_FOUND,
          "%s version required", tag);
    CHECK(gt2_iso_stat(cd, "a/b", &e) == GT2_ISO_ERR_NOT_FOUND, "%s subdir",
          tag);
    CHECK(gt2_iso_stat(cd, "", &e) == GT2_ISO_ERR_INVAL, "%s empty", tag);
    u8 *data = NULL;
    u32 n = 0;
    CHECK(gt2_iso_read(cd, "GT2.OVL;1", &data, &n) == GT2_ISO_OK &&
          n == 289752 && data[0] == 0x30, "%s ovl read n=%u head=%02x",
          tag, n, data ? data[0] : 0);
    free(data);
    gt2_cd_close(cd);
}

int main(void) {
    const char *iso = getenv("GT2_ISO");
    if (!iso || !iso[0])
        iso = "/tmp/opencode/gt2.iso";
    const char *raw = getenv("GT2_RAW_BIN");
    if (!raw || !raw[0])
        raw = RAW_DEFAULT;
    check_image(iso, "cooked");
    check_image(raw, "raw");

    if (failures == 0)
        printf("PASS test_iso (root entries ok)\n");
    else
        printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
