// gt2_car host tests: name hash vectors, carobj index build,
// binary search. Index expectations measured from the US 1.2 sim disc
// (and cross-checked against emulated 0x80011710 / 0x8005D950).

#include "gt2/car.h"
#include "gt2/vol.h"

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
    // weights w[i] = i & 0x3F (synthetic; game weights are runtime
    // data at 0x801EF630 — same vectors verified against emulated
    // 0x80060924 with identical weights).
    static u8 weights[256];
    for (int i = 0; i < 256; i++)
        weights[i] = (u8)(i & 0x3F);
    CHECK(gt2_namehash(weights, "a-a7r.cdo.gz") == 0x21B61DF2u, "hash0");
    CHECK(gt2_namehash(weights, "x2wsr.cdo.gz") == 0x38CB7CF2u, "hash1");
    CHECK(gt2_namehash(weights, "12345") == 0x31CB3D35u, "hash2");

    // carobj index (needs image; zero weights -> all hashes 0).
    const char *iso = getenv("GT2_ISO");
    if (!iso || !iso[0])
        iso = "/tmp/opencode/gt2.iso";
    gt2_vol_t *vol = NULL;
    if (gt2_vol_open(iso, &vol) != GT2_VOL_OK) {
        printf("SKIP: cannot open '%s'\n", iso);
    } else {
        static gt2_car_entry_t tab[GT2_CAR_MAX + 1];
        u32 count = 0;
        static const u8 zero[256] = { 0 };
        CHECK(gt2_car_index_build(vol, "/carobj", zero, tab,
                                  GT2_CAR_MAX + 1, &count) == GT2_CAR_OK &&
              count == 1110, "carobj count=%u", count);
        if (count == 1110) {
            CHECK(tab[0].hash == 0 && tab[0].index == 3495 && tab[0].z == 0,
                  "tab0 %08x/%u", tab[0].hash, tab[0].index);
            CHECK(tab[500].hash == 0 && tab[500].index == 5495,
                  "tab500 %u", tab[500].index);
            CHECK(tab[1109].hash == 0 && tab[1109].index == 7931,
                  "tab1109 %u", tab[1109].index);
            CHECK(tab[1110].hash == 0 && tab[1110].index == 7935,
                  "sentinel %u", tab[1110].index);
        }
        CHECK(gt2_car_index_build(vol, "/nodir", zero, tab, 10, NULL) ==
              GT2_CAR_ERR_NOT_FOUND, "missing dir");
        CHECK(gt2_car_index_build(vol, "/carobj", zero, tab, 2, NULL) ==
              GT2_CAR_ERR_NOSPACE, "small cap");
        CHECK(gt2_car_index_build(NULL, "/carobj", zero, tab, 10, NULL) ==
              GT2_CAR_ERR_INVAL, "null vol");
        gt2_vol_close(vol);
    }

    // binary search on a synthetic sorted table (same probes as the
    // emulated 0x8005D950 run).
    static const gt2_car_entry_t st[5] = {
        { 10, 100, 0 }, { 20, 101, 0 }, { 30, 102, 0 }, { 40, 103, 0 },
        { 50, 104, 0 },
    };
    CHECK(gt2_car_find(st, 5, 10) == 0, "find first");
    CHECK(gt2_car_find(st, 5, 30) == 2, "find mid");
    CHECK(gt2_car_find(st, 5, 50) == 4, "find last");
    CHECK(gt2_car_find(st, 5, 25) == -1, "miss mid");
    CHECK(gt2_car_find(st, 5, 5) == -1, "miss low");
    CHECK(gt2_car_find(st, 5, 60) == -1, "miss high");
    CHECK(gt2_car_find(st, 0, 10) == -1, "empty");
    CHECK(gt2_car_find(NULL, 5, 10) == -1, "null tab");
    CHECK(gt2_car_strerror(GT2_CAR_OK) != NULL, "strerror");

    // wheel codec (vectors verified against emulated 0x80011570;
    // maker table "bbbrduenfaozraspyo" as in SCUS rodata).
    static const u8 makers[] = "bbbrduenfaozraspyo";
    CHECK(gt2_wheel_codec(makers, 9, "bb001--s.tim") == 0x00010073u,
          "wheel0");
    CHECK(gt2_wheel_codec(makers, 9, "bb004-5g.tim") == 0x00044067u,
          "wheel1");
    CHECK(gt2_wheel_codec(makers, 9, "sp678-6s.tim") == 0x72A66073u,
          "wheel2");
    CHECK(gt2_wheel_codec(makers, 9, "bb12345") == 0x007B4000u, "wheel3");
    CHECK(gt2_wheel_codec(makers, 9, "yo00000") == 0x80000000u, "wheel4");

    if (gt2_vol_open(iso, &vol) == GT2_VOL_OK) {
        // carwheel table (emulated: 192 entries at 0x801E30F0).
        static u32 wtab[2048];
        u32 wn = 0;
        CHECK(gt2_wheel_build(vol, makers, 9, wtab, 2048, &wn) ==
              GT2_CAR_OK && wn == 192, "wheel count=%u", wn);
        if (wn == 192) {
            CHECK(wtab[0] == 0x00010073u && wtab[1] == 0x00020073u &&
                  wtab[2] == 0x00030067u, "wheel head %08x %08x %08x",
                  wtab[0], wtab[1], wtab[2]);
        }
        // engine table (emulated: 305 entries at 0x801DF340).
        static u16 etab[1024];
        u32 en = 0;
        CHECK(gt2_engine_build(vol, etab, 1024, &en) == GT2_CAR_OK &&
              en == 305, "engine count=%u", en);
        if (en == 305) {
            CHECK(etab[0] == 0 && etab[1] == 1 && etab[9] == 9,
                  "engine head %u %u %u", etab[0], etab[1], etab[9]);
            CHECK(etab[304] == 23001, "engine last %u", etab[304]);
        }
        CHECK(gt2_engine_parse("00042_n7.es") == 42, "engine parse");
        CHECK(gt2_engine_parse("abc") == 0, "engine parse empty");
        gt2_vol_close(vol);
        vol = NULL;
    }

    if (failures == 0)
        printf("PASS test_car (hash+index+find ok)\n");
    else
        printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
