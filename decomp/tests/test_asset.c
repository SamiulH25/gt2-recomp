// gt2_asset host tests: course hash + crsmap index + aligned-window load +
// CRS parse/relocate/lookup. Needs GT2_ISO (default /tmp/opencode/gt2.iso);
// SKIP when absent. All expectations measured from the US 1.2 sim disc and
// the emulated task0b1 (see decomp/docs/asset_notes.md, tools/crs_asset.py).

#include "gt2/asset.h"

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
    // Hash vectors proven against the emulated 0x80083004 + table[0].
    CHECK(gt2_crs_hash("2p_autumn") == 0xB70B31B6u, "hash 2p_autumn=%08x",
          gt2_crs_hash("2p_autumn"));
    CHECK(gt2_crs_hash("") == 0, "empty hash");
    CHECK(gt2_crs_hash(NULL) == 0, "null hash");
    CHECK(gt2_crs_hash("a") == 0x61u, "hash a=%08x", gt2_crs_hash("a"));

    const char *iso = getenv("GT2_ISO");
    if (!iso || !iso[0])
        iso = "/tmp/opencode/gt2.iso";

    gt2_vol_t *vol = NULL;
    gt2_vol_status_t vst = gt2_vol_open(iso, &vol);
    if (vst == GT2_VOL_ERR_IO) {
        printf("SKIP: cannot open '%s'\n", iso);
        return 0;
    }
    CHECK(vst == GT2_VOL_OK, "open: %s", gt2_vol_strerror(vst));
    if (!vol)
        return 1;

    // /.crsinfo is tbl index 8 (root slot 6); the game reaches it as Q2
    // cache slot 6.
    gt2_vol_entry_t e;
    CHECK(gt2_vol_stat_path(vol, "/.crsinfo", &e) == GT2_VOL_OK &&
          e.next == 8, "crsinfo tbl=%u want 8", e.next);

    // crsmap index: 120 files from tbl 8156, stems hashed in order.
    u32 table[256];
    u32 count = 0, first = 0;
    CHECK(gt2_crsmap_build(vol, table, 256, &count, &first) ==
          GT2_ASSET_OK && count == 120 && first == 8156,
          "crsmap count=%u first=%u", count, first);
    CHECK(table[0] == 0xB70B31B6u, "crsmap[0]=%08x", table[0]);
    CHECK(gt2_crsmap_build(vol, table, 10, NULL, NULL) ==
          GT2_ASSET_ERR_NOSPACE, "small cap must be NOSPACE");
    CHECK(gt2_crsmap_build(NULL, table, 256, NULL, NULL) ==
          GT2_ASSET_ERR_INVAL, "null vol");
    CHECK(gt2_crsmap_build(vol, NULL, 256, NULL, NULL) ==
          GT2_ASSET_ERR_INVAL, "null out");

    // Aligned-window load of tbl 8: 0xFC5 bytes at VOL+0x9C000 with the
    // CRS header at window+0 (0x3B before the exact file start).
    u8 *win = NULL;
    u32 wlen = 0;
    CHECK(gt2_asset_window(vol, 8, &win, &wlen) == GT2_ASSET_OK &&
          wlen == 0xFC5u, "window len=%u want %u", wlen, 0xFC5u);
    CHECK(win && !memcmp(win, "CRS", 3) && win[3] == 0, "CRS magic");
    CHECK(gt2_asset_window(vol, 11581, NULL, NULL) ==
          GT2_ASSET_ERR_INVAL, "bad idx + null out");
    CHECK(gt2_asset_window(vol, 8, NULL, NULL) == GT2_ASSET_ERR_INVAL,
          "null out");

    // Same bytes via the cache-slot indirection (slot 6 -> tbl 8).
    u16 cache[8];
    for (u32 i = 0; i < 8; i++)
        cache[i] = 0xFFFFu;
    cache[6] = 8;
    u8 *win2 = NULL;
    u32 wlen2 = 0;
    CHECK(gt2_asset_window_cached(vol, cache, 8, 6, &win2, &wlen2) ==
          GT2_ASSET_OK && wlen2 == wlen && !memcmp(win, win2, wlen),
          "cached window");
    CHECK(gt2_asset_window_cached(vol, cache, 8, 7, NULL, NULL) ==
          GT2_ASSET_ERR_INVAL, "0xFFFF slot must be INVAL");
    CHECK(gt2_asset_window_cached(vol, cache, 8, 8, NULL, NULL) ==
          GT2_ASSET_ERR_INVAL, "slot >= ncache");

    // CRS parse: 126 records; every target inside the window.
    gt2_crs_rec_t recs[256];
    u32 nrec = 0;
    CHECK(gt2_crsinfo_parse(win, wlen, recs, 256, &nrec) == GT2_ASSET_OK &&
          nrec == 126, "crs count=%u want 126", nrec);
    u32 in_win = 0;
    for (u32 i = 0; i < nrec; i++) {
        if (recs[i].off < wlen)
            in_win++;
    }
    CHECK(in_win == nrec, "targets in window %u/%u", in_win, nrec);
    // Record order follows the crsmap listing: rec[0] carries its hash.
    CHECK(recs[0].hash == table[0], "rec0 hash=%08x", recs[0].hash);
    // 120 of the 126 record hashes have a /crsmap file (6 data-rev extras).
    u32 matched = 0;
    for (u32 i = 0; i < nrec; i++) {
        for (u32 j = 0; j < count; j++) {
            if (recs[i].hash == table[j]) {
                matched++;
                break;
            }
        }
    }
    CHECK(matched == 120, "hash overlap %u want 120", matched);
    // Record targets are course display names ("Autumn Ring...").
    CHECK(!memcmp(gt2_crs_target(win, &recs[0]), "Autumn Ring", 11),
          "rec0 target names");
    // Lookup: present -> index, absent -> -1 (linear, listing order).
    CHECK(gt2_crs_find(recs, nrec, table[0]) == 0, "find first");
    CHECK(gt2_crs_find(recs, nrec, 0xDEADBEEFu) == -1, "find miss");
    CHECK(gt2_crs_find(NULL, nrec, table[0]) == -1, "find null");
    // Parse errors.
    CHECK(gt2_crsinfo_parse(win, wlen, recs, 10, NULL) ==
          GT2_ASSET_ERR_NOSPACE, "small cap");
    CHECK(gt2_crsinfo_parse(win, 10, recs, 256, NULL) ==
          GT2_ASSET_ERR_TRUNCATED, "short window");
    win[0] = 'X';
    CHECK(gt2_crsinfo_parse(win, wlen, recs, 256, NULL) ==
          GT2_ASSET_ERR_BAD_MAGIC, "bad magic");
    win[0] = 'C';
    CHECK(gt2_crsinfo_parse(NULL, wlen, recs, 256, NULL) ==
          GT2_ASSET_ERR_INVAL, "null data");
    free(win);
    free(win2);

    // Q2 index-cache build (paths from the SCUS pointer table; SKIP if
    // the SCUS file is absent). Vectors from emulated 0x80010228
    // (232 pinned, 16 x 0xFFFF regional misses).
    FILE *sf = fopen("disc/SCUS_944.88", "rb");
    if (!sf) {
        printf("SKIP: no disc/SCUS_944.88 for q2 paths\n");
    } else {
        char *q2[248];
        u32 nq2 = 0;
        u32 ptab[248];
        fseek(sf, 0x800 + (0x8009118Cu - 0x80010000u), SEEK_SET);
        int ok = fread(ptab, 4, 248, sf) == 248;
        for (u32 i = 0; ok && i < 248 && ptab[i] != 0; i++) {
            char buf[64];
            u32 at = 0;
            while (at < sizeof buf - 1) {
                fseek(sf, 0x800 + (ptab[i] - 0x80010000u) + at, SEEK_SET);
                if (fread(buf + at, 1, 1, sf) != 1)
                    break;
                if (buf[at] == '\0')
                    break;
                at++;
            }
            buf[at] = '\0';
            q2[nq2] = malloc(at + 1);
            CHECK(q2[nq2] != NULL, "oom");
            if (!q2[nq2])
                break;
            memcpy(q2[nq2], buf, at + 1);
            nq2++;
        }
        fclose(sf);
        CHECK(ok && nq2 == 248, "q2 paths=%u", nq2);
        if (nq2 == 248) {
            u16 cache[248];
            CHECK(gt2_q2_cache_build(vol, (const char *const *)q2, 248,
                                     cache) == GT2_ASSET_OK, "q2 build");
            CHECK(cache[0] == 2 && cache[6] == 8 && cache[8] == 14,
                  "q2 head %u %u %u", cache[0], cache[6], cache[8]);
            CHECK(cache[70] == 81 && cache[107] == 7964 &&
                  cache[108] == 8276 && cache[191] == 8609,
                  "q2 dirs %u %u %u %u", cache[70], cache[107],
                  cache[108], cache[191]);
            CHECK(cache[228] == 11559 && cache[229] == 11560,
                  "q2 span pair %u %u", cache[228], cache[229]);
            CHECK(cache[77] == 0xFFFFu && cache[213] == 0xFFFFu,
                  "q2 misses %u %u", cache[77], cache[213]);
            u32 npin = 0;
            for (u32 i = 0; i < 248; i++) {
                if (cache[i] != 0xFFFFu)
                    npin++;
            }
            CHECK(npin == 232, "q2 pinned=%u want 232", npin);
            // The built cache drives the slot loader end to end.
            u8 *cwin = NULL;
            u32 cwlen = 0;
            CHECK(gt2_asset_window_cached(vol, cache, 248, 6, &cwin,
                                          &cwlen) == GT2_ASSET_OK &&
                  cwlen == 0xFC5u && !memcmp(cwin, "CRS", 3),
                  "q2-driven window");
            free(cwin);
        }
        for (u32 i = 0; i < nq2; i++)
            free(q2[i]);
        CHECK(gt2_q2_cache_build(NULL, NULL, 0, NULL) ==
              GT2_ASSET_ERR_INVAL, "q2 null");
        u16 one;
        CHECK(gt2_q2_cache_build(vol, NULL, 0, &one) == GT2_ASSET_OK,
              "q2 empty");
    }

    // Raw 2352 dump must agree on the window.
    const char *raw = getenv("GT2_RAW_BIN");
    if (!raw || !raw[0])
        raw = "Gran Turismo 2 (USA) (Simulation Mode) (v1.2)/"
              "Gran Turismo 2 (USA) (Simulation Mode) (v1.2).bin";
    gt2_vol_t *rvol = NULL;
    if (gt2_vol_open(raw, &rvol) == GT2_VOL_OK) {
        u8 *rwin = NULL;
        u32 rn = 0;
        CHECK(gt2_asset_window(rvol, 8, &rwin, &rn) == GT2_ASSET_OK &&
              rn == 0xFC5u && !memcmp(rwin, "CRS", 3), "raw window");
        free(rwin);
        gt2_vol_close(rvol);
    } else {
        printf("SKIP: cannot open raw '%s'\n", raw);
    }

    gt2_vol_close(vol);

    if (failures == 0)
        printf("PASS test_asset (crsmap=120 crs=126 window=0xFC5 ok)\n");
    else
        printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
