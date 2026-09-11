// gt2_tim host tests. Needs a cooked 2048-B/sector ISO via GT2_ISO
// (default /tmp/opencode/gt2.iso). Prints SKIP and exits 0 when the image
// is absent so plain builds never break. All expectations were measured
// from the US 1.2 sim disc (see gt2/tim.h).

#include "gt2/tim.h"
#include "gt2/vol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

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

    gt2_vol_t *vol = NULL;
    gt2_vol_status_t vst = gt2_vol_open(iso, &vol);
    if (vst == GT2_VOL_ERR_IO) {
        printf("SKIP: cannot open '%s' (%s)\n", iso, gt2_vol_strerror(vst));
        return 0;
    }
    CHECK(vst == GT2_VOL_OK, "open: %s", gt2_vol_strerror(vst));
    if (!vol)
        return 1;

    // arc_topmenu: 118784 gzip bytes -> 554480 raw = 12 chained TIMs.
    // NOTE: the file concatenates one gzip member per TIM at 2048-aligned
    // slots (OVL-style), so plain inflate()/uncompress() stop after the
    // first member (122900 B). gt2_gunzip_join walks the members.
    u8 *gz = NULL;
    u32 gzlen = 0;
    CHECK(gt2_vol_read(vol, "arc_topmenu", &gz, &gzlen) == GT2_VOL_OK &&
          gzlen == 118784,
          "arc_topmenu read");
    u8 *raw = NULL;
    u32 rawlen = 0;
    CHECK(gt2_gunzip_join(gz, gzlen, &raw, &rawlen) == GT2_TIM_OK &&
          rawlen == 554480,
          "arc_topmenu join (%u)", rawlen);
    free(gz);
    if (!raw)
        return 1;

    u32 n = 0;
    CHECK(gt2_tim_count(raw, (u32)rawlen, &n) == GT2_TIM_OK && n == 12,
          "tim chain count=%u want 12", n);
    for (u32 i = 0; i < 12; i++) {
        gt2_tim_info_t info;
        CHECK(gt2_tim_info(raw, (u32)rawlen, i, &info) == GT2_TIM_OK,
              "tim %u info", i);
        u32 ew = i < 4 ? 512 : 140, eh = i < 4 ? 120 : 28;
        CHECK(info.mode == 2 && !info.has_clut && info.w == ew &&
              info.h == eh,
              "tim %u: mode=%u clut=%u %ux%u", i, info.mode,
              info.has_clut, info.w, info.h);
        // Round trip: re-emit from parsed pieces == original slice.
        u8 *enc = NULL;
        u32 enclen = 0;
        gt2_tim_status_t es =
            gt2_tim_encode(&info, NULL, raw + info.pix_off, &enc, &enclen);
        CHECK(es == GT2_TIM_OK, "tim %u encode", i);
        if (es == GT2_TIM_OK) {
            // Slice starts at the running offset: recompute via walk.
            u32 off = 0;
            for (u32 k = 0; k < i; k++) {
                gt2_tim_info_t prev;
                gt2_tim_info(raw, (u32)rawlen, k, &prev);
                off += prev.total_len;
            }
            CHECK(enclen == info.total_len &&
                  memcmp(enc, raw + off, enclen) == 0,
                  "tim %u round-trip", i);
            free(enc);
        }
    }
    gt2_tim_info_t dummy;
    CHECK(gt2_tim_info(raw, (u32)rawlen, 12, &dummy) == GT2_TIM_ERR_NOT_FOUND,
          "tim index past chain");
    CHECK(gt2_tim_count(NULL, 0, &n) == GT2_TIM_ERR_INVAL, "count NULL");

    // Decode tim 0: 512x120, first pixel 0x8000 (STP black) -> (0,0,0).
    u8 *rgb = NULL;
    u32 w = 0, h = 0;
    CHECK(gt2_tim_decode(raw, (u32)rawlen, 0, &rgb, &w, &h) == GT2_TIM_OK &&
          w == 512 && h == 120 && rgb[0] == 0 && rgb[1] == 0 &&
          rgb[2] == 0,
          "decode tim0 %ux%u rgb=(%u,%u,%u)", w, h,
          rgb ? rgb[0] : 9, rgb ? rgb[1] : 9, rgb ? rgb[2] : 9);
    free(rgb);
    free(raw);

    // Logo container: opaque head + trailing CLUT TIM at a strict offset.
    u8 *logo = NULL;
    u32 logolen = 0;
    CHECK(gt2_vol_read_path(vol, "/carlogo/a-a7rl--.tim", &logo,
                            &logolen) == GT2_VOL_OK && logolen == 6368,
          "logo read");
    u32 loff = 0;
    CHECK(gt2_logo_find_tim(logo, logolen, &loff) == GT2_TIM_OK &&
          loff == 5184,
          "logo tim @%u", loff);
    gt2_tim_info_t li;
    CHECK(gt2_tim_validate(logo, logolen, loff, 1, &li) == GT2_TIM_OK &&
          li.mode == 0 && li.has_clut && li.clut_colors == 16 &&
          li.w == 62 && li.h == 56,
          "logo tim: mode=%u clut=%u %ux%u", li.mode, li.clut_colors,
          li.w, li.h);
    // The declared image runs past EOF (consumer RE open): decode of the
    // full frame must fail closed, not read out of bounds.
    u8 *lrgb = NULL;
    CHECK(gt2_tim_decode(logo, logolen, 0, &lrgb, NULL, NULL) ==
          GT2_TIM_ERR_NOT_FOUND,
          "logo container is not a TIM chain");
    free(lrgb);
    free(logo);

    // TXD string dictionary.
    u8 *txd = NULL;
    u32 txdlen = 0;
    CHECK(gt2_vol_read_path(vol, "/.text/data-race.txd", &txd,
                            &txdlen) == GT2_VOL_OK && txdlen == 44983,
          "txd read");
    CHECK(gt2_txd_count(txd, txdlen) == 2293, "txd count=%u",
          gt2_txd_count(txd, txdlen));
    const u8 *s = NULL;
    u32 sl = 0;
    CHECK(gt2_txd_get(txd, txdlen, 0, &s, &sl) == GT2_TIM_OK && sl == 6 &&
          memcmp(s, "%dLaps", 6) == 0,
          "txd[0]");
    CHECK(gt2_txd_get(txd, txdlen, 1, &s, &sl) == GT2_TIM_OK && sl == 9 &&
          memcmp(s, "Wrong Way", 9) == 0,
          "txd[1]");
    CHECK(gt2_txd_get(txd, txdlen, 2, &s, &sl) == GT2_TIM_OK && sl == 17 &&
          memcmp(s, "Too Fast To Stop!", 17) == 0,
          "txd[2]");
    CHECK(gt2_txd_get(txd, txdlen, 2293, &s, &sl) == GT2_TIM_ERR_NOT_FOUND,
          "txd OOB");
    CHECK(gt2_txd_get(NULL, 0, 0, &s, &sl) == GT2_TIM_ERR_INVAL,
          "txd NULL");
    free(txd);

    // champtim.tim: 32 zero bytes + 16500 B opaque payload (dims open).
    u8 *ch = NULL;
    u32 chlen = 0;
    CHECK(gt2_vol_read(vol, "champtim.tim", &ch, &chlen) == GT2_VOL_OK &&
          chlen == 16532,
          "champtim read");
    int zeros = 1;
    for (u32 i = 0; i < 32; i++)
        if (ch[i])
            zeros = 0;
    CHECK(zeros, "champtim 32B zero header");
    u32 tcount = 0;
    CHECK(gt2_tim_count(ch, chlen, &tcount) == GT2_TIM_OK && tcount == 0,
          "champtim holds no TIM");
    free(ch);

    // Rejections.
    u8 bad[8] = { 0x11, 0, 0, 0, 2, 0, 0, 0 };
    CHECK(gt2_tim_validate(bad, sizeof bad, 0, 0, &dummy) ==
          GT2_TIM_ERR_MAGIC,
          "bad magic");
    bad[0] = 0x10;
    bad[4] = 0xFF;
    CHECK(gt2_tim_validate(bad, sizeof bad, 0, 0, &dummy) ==
          GT2_TIM_ERR_MODE,
          "bad mode");

    gt2_vol_close(vol);
    if (failures == 0)
        printf("PASS test_tim (chain + logo + txd ok)\n");
    return failures != 0;
}
