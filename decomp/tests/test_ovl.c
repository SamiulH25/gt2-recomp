// gt2_ovl host tests: header parse + member inflate vs reference splits.
// GT2_ISO / GT2_RAW_BIN as in test_cd.c. SKIP when an image is absent.
// Reference: overlays/gt2_0{1..6}.exe in the repo (byte-exact expected).

#include "gt2/cd.h"
#include "gt2/iso.h"
#include "gt2/ovl.h"

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

static const u32 want_decomp[GT2_OVL_MEMBERS] = {
    316920, 248004, 275780, 11500, 273012, 8416,
};
static const u32 want_comp[GT2_OVL_MEMBERS] = {
    144709, 44333, 53389, 5195, 38461, 3602,
};

static void check_image(const char *path, const char *tag) {
    gt2_cd_t *cd = NULL;
    if (gt2_cd_open(path, &cd) != GT2_CD_OK) {
        printf("SKIP: cannot open '%s'\n", path);
        return;
    }
    gt2_ovl_t *ovl = NULL;
    CHECK(gt2_ovl_open(cd, &ovl) == GT2_OVL_OK, "%s ovl open", tag);
    if (!ovl) {
        gt2_cd_close(cd);
        return;
    }
    CHECK(gt2_ovl_member_count(ovl) == GT2_OVL_MEMBERS, "%s count", tag);
    char refname[128];
    for (u32 i = 0; i < GT2_OVL_MEMBERS; i++) {
        u32 off = 0, size = 0;
        CHECK(gt2_ovl_member_range(ovl, i, &off, &size) == GT2_OVL_OK &&
              size == want_comp[i], "%s m%u comp=%u want %u", tag, i,
              size, want_comp[i]);
        u8 *data = NULL;
        u32 n = 0;
        CHECK(gt2_ovl_read_member(ovl, i, &data, &n) == GT2_OVL_OK &&
              n == want_decomp[i], "%s m%u decomp=%u want %u", tag, i, n,
              want_decomp[i]);
        snprintf(refname, sizeof refname, "overlays/gt2_0%u.exe", i + 1);
        FILE *f = fopen(refname, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long rn = ftell(f);
            fseek(f, 0, SEEK_SET);
            u8 *ref = malloc(rn > 0 ? (size_t)rn : 1);
            size_t got = fread(ref, 1, rn > 0 ? (size_t)rn : 0, f);
            fclose(f);
            CHECK(got == (size_t)rn && rn == (long)n &&
                  !memcmp(ref, data, n), "%s m%u differs from %s", tag,
                  i, refname);
            free(ref);
        } else {
            printf("SKIP: no %s\n", refname);
        }
        free(data);
    }
    CHECK(gt2_ovl_member_range(ovl, 6, NULL, NULL) == GT2_OVL_ERR_INVAL,
          "%s idx 6 must be INVAL", tag);
    gt2_ovl_close(ovl);
    gt2_ovl_close(NULL);
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
        printf("PASS test_ovl (6 members byte-exact)\n");
    else
        printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
