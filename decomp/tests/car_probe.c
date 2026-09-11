// car_probe: build car data indexes and dump them for xcheck.
// Usage: car_probe <iso> [carobj|wheel|engine]
//   carobj: "COUNT <n>", n lines "IDX <tblidx>", "SENTINEL <t>"
//   wheel:  "COUNT <n>", n lines "WHEEL <hex>"
//   engine: "COUNT <n>", n lines "ENG <val>"
#include "gt2/car.h"
#include "gt2/vol.h"

#include <stdio.h>
#include <string.h>

static const u8 zero[256] = { 0 };
static const u8 makers[] = "bbbrduenfaozraspyo";

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: car_probe <iso> [carobj|wheel|engine]\n");
        return 2;
    }
    const char *which = argc > 2 ? argv[2] : "carobj";
    gt2_vol_t *vol = NULL;
    if (gt2_vol_open(argv[1], &vol) != GT2_VOL_OK) {
        fprintf(stderr, "open failed\n");
        return 1;
    }
    if (!strcmp(which, "carobj")) {
        static gt2_car_entry_t tab[GT2_CAR_MAX + 1];
        u32 count = 0;
        if (gt2_car_index_build(vol, "/carobj", zero, tab, GT2_CAR_MAX + 1,
                                &count) != GT2_CAR_OK) {
            fprintf(stderr, "build failed\n");
            return 1;
        }
        printf("COUNT %u\n", count);
        for (u32 i = 0; i < count; i++)
            printf("IDX %u\n", tab[i].index);
        printf("SENTINEL %u\n", tab[count].index);
    } else if (!strcmp(which, "wheel")) {
        static u32 tab[2048];
        u32 count = 0;
        if (gt2_wheel_build(vol, makers, 9, tab, 2048, &count) != GT2_CAR_OK) {
            fprintf(stderr, "build failed\n");
            return 1;
        }
        printf("COUNT %u\n", count);
        for (u32 i = 0; i < count; i++)
            printf("WHEEL %08x\n", tab[i]);
    } else if (!strcmp(which, "engine")) {
        static u16 tab[1024];
        u32 count = 0;
        if (gt2_engine_build(vol, tab, 1024, &count) != GT2_CAR_OK) {
            fprintf(stderr, "build failed\n");
            return 1;
        }
        printf("COUNT %u\n", count);
        for (u32 i = 0; i < count; i++)
            printf("ENG %u\n", tab[i]);
    } else {
        fprintf(stderr, "unknown %s\n", which);
        return 2;
    }
    gt2_vol_close(vol);
    return 0;
}
