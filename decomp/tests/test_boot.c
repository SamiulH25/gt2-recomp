// gt2_boot host tests: full storage chain on the disc image.

#include "gt2/boot.h"

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

int main(void) {
    const char *iso = getenv("GT2_ISO");
    if (!iso || !iso[0])
        iso = "/tmp/opencode/gt2.iso";

    gt2_boot_t *b = NULL;
    gt2_boot_status_t st = gt2_boot_open(iso, &b);
    if (st == GT2_BOOT_ERR_IO) {
        printf("SKIP: cannot open '%s'\n", iso);
        return 0;
    }
    CHECK(st == GT2_BOOT_OK, "open: %s", gt2_boot_strerror(st));
    if (!b)
        return 1;
    CHECK(gt2_boot_cd(b) != NULL, "cd");
    CHECK(gt2_vol_file_count(gt2_boot_vol(b)) == 11581, "vol files");
    CHECK(gt2_vol_slot_count(gt2_boot_vol(b)) == 11620, "vol slots");
    CHECK(gt2_ovl_member_count(gt2_boot_ovl(b)) == GT2_OVL_MEMBERS,
          "ovl members");

    // end-to-end spot checks through one handle
    u32 off = 0, size = 0;
    CHECK(gt2_vol_find_path(gt2_boot_vol(b), "/arcade/arc_carlogo", &off,
                            &size) == GT2_VOL_OK && size == 345948,
          "find_path");
    u8 *ov = NULL;
    u32 n = 0;
    CHECK(gt2_boot_load_overlay(b, 1, &ov, &n) == GT2_VOL_OK &&
          n == 248004, "overlay1 n=%u", n);
    if (ov) {
        // cross-check head/tail against the reference split
        FILE *f = fopen("overlays/gt2_02.exe", "rb");
        if (f) {
            u8 head[16], tail[16];
            size_t rn = fread(head, 1, sizeof head, f);
            fseek(f, -16, SEEK_END);
            size_t rn2 = fread(tail, 1, sizeof tail, f);
            fclose(f);
            CHECK(rn == sizeof head && rn2 == sizeof tail &&
                  !memcmp(ov, head, 16) && !memcmp(ov + n - 16, tail, 16),
                  "overlay1 bytes");
        } else {
            printf("SKIP: no overlays/gt2_02.exe\n");
        }
        free(ov);
    }
    CHECK(gt2_boot_load_overlay(b, 6, NULL, NULL) == GT2_BOOT_ERR_INVAL,
          "bad idx");
    CHECK(gt2_boot_load_overlay(NULL, 0, NULL, NULL) ==
          GT2_BOOT_ERR_INVAL, "null boot");
    gt2_boot_close(b);
    gt2_boot_close(NULL);

    if (failures == 0)
        printf("PASS test_boot (chain ok)\n");
    else
        printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
