// gt2_vol_pack host tests. Needs a cooked 2048-B/sector ISO via GT2_ISO
// (default /tmp/opencode/gt2.iso). Prints SKIP and exits 0 when the image
// is absent so plain builds never break.
//
// Covers: zero-rep byte-identical round trip, same-size replace (only the
// target span differs), grow+shrink replaces (neighbors intact, table
// monotonic, tree walk + span still resolve, files 0/1 byte-identical),
// rejection of bad indices/duplicates/bad args, open_mem backend.

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

// Three smallest nonzero named files (by size): victims for
// same-size / grow / shrink. Plus two untouched neighbors to verify.
static char pick_name[3][64];
static u32 pick_index[3];
static u32 pick_size[3];
static int pick_n = 0;

struct pick_ctx {
    gt2_vol_t *vol;
};

static void pick_visit(const char *name, u32 index, void *ctx) {
    struct pick_ctx *c = ctx;
    u32 off = 0, size = 0;
    if (gt2_vol_file_range(c->vol, index, &off, &size) != GT2_VOL_OK)
        return;
    if (size == 0 || size > (1u << 20))
        return;
    // Insertion into top-3 smallest (simple, names are few-KB scanned).
    for (int k = 0; k < 3; k++) {
        if (pick_n <= k || size < pick_size[k]) {
            for (int j = 2; j > k; j--) {
                if (pick_n > j - 1) {
                    memcpy(pick_name[j], pick_name[j - 1], sizeof pick_name[j]);
                    pick_index[j] = pick_index[j - 1];
                    pick_size[j] = pick_size[j - 1];
                }
            }
            snprintf(pick_name[k], sizeof pick_name[k], "%s", name);
            pick_index[k] = index;
            pick_size[k] = size;
            if (pick_n < 3)
                pick_n++;
            return;
        }
    }
}

static u8 *read_all(gt2_vol_t *vol, u32 index, u32 *size_out) {
    u32 off = 0, size = 0;
    if (gt2_vol_file_range(vol, index, &off, &size) != GT2_VOL_OK)
        return NULL;
    u8 *buf = malloc(size ? size : 1);
    if (!buf)
        return NULL;
    if (size > 0 &&
        gt2_vol_pread(vol, off, buf, size) != GT2_VOL_OK) {
        free(buf);
        return NULL;
    }
    if (size_out)
        *size_out = size;
    return buf;
}

int main(void) {
    const char *iso = getenv("GT2_ISO");
    if (!iso || !iso[0])
        iso = "/tmp/opencode/gt2.iso";

    gt2_vol_t *vol = NULL;
    gt2_vol_status_t st = gt2_vol_open(iso, &vol);
    if (st == GT2_VOL_ERR_IO) {
        printf("SKIP: cannot open '%s' (%s)\n", iso, gt2_vol_strerror(st));
        return 0;
    }
    CHECK(st == GT2_VOL_OK, "open: %s", gt2_vol_strerror(st));
    if (!vol)
        return 1;
    u32 dc = gt2_vol_file_count(vol);
    CHECK(dc == 11581, "file_count=%u want 11581", dc);

    struct pick_ctx ctx = { vol };
    gt2_vol_visit_names(vol, pick_visit, &ctx);
    CHECK(pick_n == 3, "picked %d small files", pick_n);
    for (int k = 0; k < pick_n; k++)
        CHECK(pick_index[k] >= 2 && pick_index[k] <= dc - 2,
              "pick %d index %u out of replaceable range",
              k, pick_index[k]);
    // Victims must be distinct indices.
    CHECK(pick_index[0] != pick_index[1] &&
          pick_index[1] != pick_index[2] &&
          pick_index[0] != pick_index[2],
          "victim indices collide");

    // Snapshot original bytes of victims + neighbors (index +/- 1).
    u32 nb_idx[2] = { pick_index[0] + 1, pick_index[1] > 2 ?
                      pick_index[1] - 1 : pick_index[1] + 2 };
    for (int k = 0; k < 2; k++)   // keep neighbors off the victim set
        for (int v = 0; v < 3; v++)
            if (nb_idx[k] == pick_index[v])
                nb_idx[k] += 4;
    u32 nb_size[2] = { 0, 0 };
    u8 *nb_data[2] = { read_all(vol, nb_idx[0], &nb_size[0]),
                       read_all(vol, nb_idx[1], &nb_size[1]) };
    CHECK(nb_data[0] && nb_data[1], "neighbor snapshots");

    u32 v0_size = 0, v1_size = 0, v2_size = 0;
    u8 *v0_orig = read_all(vol, pick_index[0], &v0_size);
    u8 *v1_orig = read_all(vol, pick_index[1], &v1_size);
    u8 *v2_orig = read_all(vol, pick_index[2], &v2_size);
    CHECK(v0_orig && v1_orig && v2_orig, "victim snapshots");

    // 1. Zero-rep round trip: byte-identical extent.
    u8 *blob0 = NULL;
    u32 blob0_size = 0;
    CHECK(gt2_vol_pack(iso, NULL, 0, &blob0, &blob0_size) == GT2_VOL_OK,
          "pack zero reps");
    if (blob0) {
        u32 off = 0, size = 0;
        CHECK(gt2_vol_file_range(vol, dc - 2, &off, &size) == GT2_VOL_OK,
              "last valid range");
        // blob covers [0, end of file dc-2), VOL-relative.
        u32 expect = (u32)((off - GT2_VOL_BASE) + size);
        CHECK(blob0_size == expect, "blob size %u want %u",
              blob0_size, expect);
        // Streaming compare against the source extent.
        u8 *chunk = malloc(1u << 20);
        u32 done = 0;
        int diff = 0;
        while (chunk && done < blob0_size) {
            u32 n = blob0_size - done;
            if (n > (1u << 20))
                n = (1u << 20);
            if (gt2_vol_pread(vol, GT2_VOL_BASE + done, chunk, n) != GT2_VOL_OK) {
                diff = -1;
                break;
            }
            if (memcmp(chunk, blob0 + done, n) != 0) {
                diff = 1;
                break;
            }
            done += n;
        }
        free(chunk);
        CHECK(diff == 0, "zero-rep blob differs from source (diff=%d)", diff);

        // Mem backend parses it identically.
        gt2_vol_t *mv = NULL;
        CHECK(gt2_vol_open_mem(blob0, blob0_size, &mv) == GT2_VOL_OK,
              "open_mem: %s",
              mv ? "ok" : "parse failed");
        if (mv) {
            CHECK(gt2_vol_file_count(mv) == 11581, "mem files");
            CHECK(gt2_vol_name_count(mv) == 11568, "mem names");
            CHECK(gt2_vol_slot_count(mv) == 11620, "mem slots");
            u8 *arc = NULL;
            u32 an = 0;
            CHECK(gt2_vol_read(mv, "arc_topmenu", &arc, &an) == GT2_VOL_OK &&
                  an == 118784 && arc[0] == 0x1F && arc[1] == 0x8B,
                  "mem arc_topmenu");
            free(arc);
            gt2_vol_close(mv);
        }
    }

    // 2. Same-size replace of victim 0 with a pattern.
    u8 *pat0 = malloc(pick_size[0] ? pick_size[0] : 1);
    for (u32 i = 0; i < pick_size[0]; i++)
        pat0[i] = (u8)(0xA5 ^ (i * 31));
    gt2_vol_replacement_t r0 = { pick_index[0], pat0, pick_size[0] };
    u8 *blob1 = NULL;
    u32 blob1_size = 0;
    CHECK(gt2_vol_pack(iso, &r0, 1, &blob1, &blob1_size) == GT2_VOL_OK,
          "pack same-size");
    if (blob0 && blob1) {
        CHECK(blob1_size == blob0_size, "same-size keeps extent size");
        u32 off = 0, size = 0;
        gt2_vol_file_range(vol, pick_index[0], &off, &size);
        u32 rel = off - GT2_VOL_BASE;   // VOL-relative span of victim 0
        CHECK(size == pick_size[0], "victim size stable");
        CHECK(memcmp(blob1, blob0, rel) == 0, "prefix before victim intact");
        CHECK(memcmp(blob1 + rel + size, blob0 + rel + size,
                     blob0_size - rel - size) == 0,
              "suffix after victim intact");
        CHECK(memcmp(blob1 + rel, pat0, size) == 0, "victim holds pattern");
        gt2_vol_t *m1 = NULL;
        CHECK(gt2_vol_open_mem(blob1, blob1_size, &m1) == GT2_VOL_OK,
              "open_mem repacked");
        if (m1) {
            u32 n = 0;
            u8 *back = read_all(m1, pick_index[0], &n);
            CHECK(back && n == size && memcmp(back, pat0, n) == 0,
                  "mem read-back of pattern");
            free(back);
            gt2_vol_close(m1);
        }
    }

    // 3. Grow victim 1 (+1000) + shrink victim 2 (half).
    u32 grow_size = v1_size + 1000;
    u8 *grow = malloc(grow_size);
    for (u32 i = 0; i < grow_size; i++)
        grow[i] = (u8)(0x3C ^ (i * 17));
    u32 shrink_size = v2_size / 2;
    u8 *shrink = malloc(shrink_size ? shrink_size : 1);
    for (u32 i = 0; i < shrink_size; i++)
        shrink[i] = (u8)(0x71 ^ (i * 7));
    gt2_vol_replacement_t reps[2] = {
        { pick_index[1], grow, grow_size },
        { pick_index[2], shrink, shrink_size },
    };
    u8 *blob2 = NULL;
    u32 blob2_size = 0;
    CHECK(gt2_vol_pack(iso, reps, 2, &blob2, &blob2_size) == GT2_VOL_OK,
          "pack grow+shrink");
    if (blob2) {
        gt2_vol_t *m2 = NULL;
        CHECK(gt2_vol_open_mem(blob2, blob2_size, &m2) == GT2_VOL_OK,
              "open_mem grown");
        if (m2) {
            u32 n = 0;
            u8 *g = read_all(m2, pick_index[1], &n);
            CHECK(g && n == grow_size && memcmp(g, grow, n) == 0,
                  "grown bytes");
            free(g);
            u8 *s = read_all(m2, pick_index[2], &n);
            CHECK(s && n == shrink_size && memcmp(s, shrink, n) == 0,
                  "shrunk bytes");
            free(s);
            // Untouched neighbors fully intact (they shifted by +1000).
            for (int k = 0; k < 2; k++) {
                u8 *nb = read_all(m2, nb_idx[k], &n);
                CHECK(nb && n == nb_size[k] &&
                      memcmp(nb, nb_data[k], n) == 0,
                      "neighbor %u intact", nb_idx[k]);
                free(nb);
            }
            // Untouched victim 0 intact here (reps only touched 1,2).
            u8 *u = read_all(m2, pick_index[0], &n);
            CHECK(u && n == v0_size && memcmp(u, v0_orig, n) == 0,
                  "unrelated victim intact");
            free(u);
            // Table monotonic (except the degenerate final marker) and
            // files 0/1 byte-identical.
            u32 prev = 0;
            int mono = 1;
            for (u32 i = 0; i < dc; i++) {
                u32 o = 0, sz = 0;
                if (gt2_vol_file_range(m2, i, &o, &sz) != GT2_VOL_OK) {
                    if (i != dc - 1)
                        mono = 0;
                    continue;
                }
                if (o - GT2_VOL_BASE < prev)
                    mono = 0;
                prev = o - GT2_VOL_BASE;
            }
            CHECK(mono, "repacked table monotonic");
            u8 *f0 = read_all(m2, 0, &n);
            u32 on = 0;
            u8 *f0o = read_all(vol, 0, &on);
            // File 0 embeds the offset array itself ([0x10,0xB508),
            // overlapping file 0 over [0x2FC,...)), so on resize only the
            // post-array region [0xB508, file0 end) (zero pad + slots 0..27)
            // survives. The stamp covers the whole array including its
            // last-32 tail at [0xB488,0xB508).
            u32 f0keep = 0xB508u - 0x2FCu;
            CHECK(f0 && f0o && n == on && n == 0xBB80u - 0x2FCu &&
                  memcmp(f0 + f0keep, f0o + f0keep, n - f0keep) == 0,
                  "file 0 post-table region identical");
            free(f0);
            free(f0o);
            u8 *f1 = read_all(m2, 1, &n);
            u8 *f1o = read_all(vol, 1, &on);
            CHECK(f1 && f1o && n == on && memcmp(f1, f1o, n) == 0,
                  "file 1 identical");
            free(f1);
            free(f1o);
            // Tree walk + span still resolve on the repacked image.
            gt2_vol_entry_t e;
            CHECK(gt2_vol_stat_path(m2, "/arcade/arc_carlogo", &e) ==
                  GT2_VOL_OK && e.next == 15,
                  "mem tree walk");
            u32 span = 0;
            CHECK(gt2_vol_span(m2, "/replay/scea.000", "/replay/scea.999",
                               &span) == GT2_VOL_OK && span == 0,
                  "mem span");
            // Array tail at [0xB488,0xB508) holds the new last-32 tbl
            // values as part of the stamp (no separate copy exists).
            {
                u8 mirror[128];
                CHECK(gt2_vol_pread(m2, GT2_VOL_BASE + 0xB488, mirror,
                                    sizeof mirror) == GT2_VOL_OK,
                      "mirror readable");
                int mok = 1;
                for (u32 k = 0; k < 32; k++) {
                    u32 idx = dc + 1 - 32 + k;
                    u32 want = 0;
                    if (idx == dc) {
                        want = 0;   // degenerate end marker, preserved
                    } else if (idx == dc - 1) {
                        want = blob2_size;   // end of last valid file
                    } else {
                        u32 o = 0, sz = 0;
                        if (gt2_vol_file_range(m2, idx, &o, &sz) !=
                            GT2_VOL_OK) {
                            mok = 0;
                            continue;
                        }
                        want = o - GT2_VOL_BASE;
                    }
                    u32 got = 0;
                    memcpy(&got, mirror + k * 4, 4);
                    if (got != want)
                        mok = 0;
                }
                CHECK(mok, "tail mirror matches new tbl");
            }
            gt2_vol_close(m2);
        }
        free(blob2);
    }

    // 4. Rejections.
    u8 *junk = NULL;
    u8 one = 0xAA;
    gt2_vol_replacement_t bad = { 0, &one, 1 };
    CHECK(gt2_vol_pack(iso, &bad, 1, &junk, NULL) == GT2_VOL_ERR_INVAL,
          "index 0 rejected");
    bad.index = 1;
    CHECK(gt2_vol_pack(iso, &bad, 1, &junk, NULL) == GT2_VOL_ERR_INVAL,
          "index 1 rejected");
    bad.index = dc - 1;
    CHECK(gt2_vol_pack(iso, &bad, 1, &junk, NULL) == GT2_VOL_ERR_INVAL,
          "degenerate index rejected");
    bad.index = dc;
    CHECK(gt2_vol_pack(iso, &bad, 1, &junk, NULL) == GT2_VOL_ERR_INVAL,
          "out-of-range index rejected");
    gt2_vol_replacement_t dup[2] = {
        { pick_index[0], &one, 1 }, { pick_index[0], &one, 1 },
    };
    CHECK(gt2_vol_pack(iso, dup, 2, &junk, NULL) == GT2_VOL_ERR_INVAL,
          "duplicate index rejected");
    gt2_vol_replacement_t nulldata = { pick_index[0], NULL, 16 };
    CHECK(gt2_vol_pack(iso, &nulldata, 1, &junk, NULL) == GT2_VOL_ERR_INVAL,
          "NULL data rejected");
    CHECK(gt2_vol_pack(NULL, &r0, 1, &junk, NULL) == GT2_VOL_ERR_INVAL,
          "NULL image rejected");
    CHECK(gt2_vol_pack(iso, &r0, 1, NULL, NULL) == GT2_VOL_ERR_INVAL,
          "NULL blob_out rejected");
    {
        u8 *zb = NULL;
        CHECK(gt2_vol_pack(iso, &r0, 0, &zb, NULL) == GT2_VOL_OK,
              "rep_count 0 ignores reps pointer");
        if (zb && blob0)
            CHECK(memcmp(zb, blob0, blob0_size) == 0,
                  "rep_count-0 blob matches zero-rep blob");
        free(zb);
    }
    gt2_vol_t *mo = NULL;
    CHECK(gt2_vol_open_mem(NULL, 100, &mo) == GT2_VOL_ERR_INVAL,
          "open_mem NULL rejected");
    u8 tiny[16] = { 0 };
    CHECK(gt2_vol_open_mem(tiny, sizeof tiny, &mo) == GT2_VOL_ERR_BAD_MAGIC,
          "open_mem garbage rejected");

    // Optional: dump the zero-rep blob for cross-checking against
    // tools/vol_pack.py (independent implementation).
    const char *dump = getenv("GT2_VOL_PACK_OUT");
    if (dump && dump[0] && blob0 && failures == 0) {
        FILE *f = fopen(dump, "wb");
        if (f) {
            size_t w = fwrite(blob0, 1, blob0_size, f);
            printf("dump %s: %zu/%u bytes\n", dump, w, blob0_size);
            if (w != blob0_size)
                failures++;
            fclose(f);
        } else {
            printf("FAIL cannot write '%s'\n", dump);
            failures++;
        }
    }

    free(pat0);
    free(grow);
    free(shrink);
    free(v0_orig);
    free(v1_orig);
    free(v2_orig);
    free(nb_data[0]);
    free(nb_data[1]);
    free(blob0);
    free(blob1);
    gt2_vol_close(vol);

    if (failures == 0)
        printf("PASS test_vol_pack (round-trip + replace + mem ok)\n");
    return failures != 0;

}
