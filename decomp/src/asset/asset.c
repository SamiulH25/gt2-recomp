// GT2 native port: asset batch loading (course tables).
// Mirrors SCUS 0x80083004 (hash), 0x80011B70 (crsmap index), 0x8005D848 /
// 0x8005D7D0 (aligned-window load), 0x8005D8A0 (cache-slot load), 0x80011C70
// tail (crsinfo relocate). Verified by emulation; see gt2/asset.h.

#include "gt2/asset.h"

#include <stdlib.h>
#include <string.h>

const char *gt2_asset_strerror(gt2_asset_status_t st) {
    switch (st) {
    case GT2_ASSET_OK: return "ok";
    case GT2_ASSET_ERR_INVAL: return "invalid argument";
    case GT2_ASSET_ERR_NO_MEM: return "out of memory";
    case GT2_ASSET_ERR_IO: return "i/o error";
    case GT2_ASSET_ERR_BAD_MAGIC: return "bad CRS magic";
    case GT2_ASSET_ERR_TRUNCATED: return "truncated window";
    case GT2_ASSET_ERR_NOSPACE: return "table full";
    case GT2_ASSET_ERR_NOT_FOUND: return "directory not found";
    default: return "unknown";
    }
}

u32 gt2_crs_hash(const char *name) {
    // Mirrors 0x80083004: v = rol(v,6) + byte per char until NUL.
    if (!name)
        return 0;
    u32 v = 0;
    for (const u8 *p = (const u8 *)name; *p; p++)
        v = ((v << 6) | (v >> 26)) + *p;
    return v;
}

struct collect {
    gt2_vol_entry_t *buf;
    u32 n;
    u32 cap;
};

static void collect_cb(const gt2_vol_entry_t *e, void *ctx) {
    struct collect *c = ctx;
    if (c->n < c->cap)
        c->buf[c->n++] = *e;
}

gt2_asset_status_t gt2_crsmap_build(const gt2_vol_t *vol, u32 *out, u32 cap,
                                    u32 *count_out, u32 *first_idx_out) {
    if (!vol || !out || cap == 0)
        return GT2_ASSET_ERR_INVAL;

    // Snapshot the listing (game scans raw slots; list_dir visits the same
    // range in the same order).
    gt2_vol_entry_t *listing = malloc(12288 * sizeof *listing);
    if (!listing)
        return GT2_ASSET_ERR_NO_MEM;
    struct collect c = { listing, 0, 12288 };
    gt2_vol_status_t st = gt2_vol_list_dir(vol, "/crsmap", collect_cb, &c);
    if (st != GT2_VOL_OK) {
        free(listing);
        return st == GT2_VOL_ERR_NOT_FOUND ? GT2_ASSET_ERR_NOT_FOUND
                                           : GT2_ASSET_ERR_INVAL;
    }
    u32 nl = c.n;
    gt2_asset_status_t rc = GT2_ASSET_OK;
    u32 n = 0;
    u32 first = 0;

    // Skip phase: advance past leading dirs ('..'), landing on the first
    // file — mirrors the game's flag&3 skip (bounded here).
    u32 i = 0;
    while (i < nl && (listing[i].flags & 3u) == 1u)
        i++;
    if (i >= nl) {
        rc = GT2_ASSET_ERR_TRUNCATED;
        goto done;
    }
    first = listing[i].next;
    // Take files until a dir entry or the listing end (game exits on
    // flags&3 == 1, i.e. the next dir's '..' link after the END file).
    for (; i < nl; i++) {
        if ((listing[i].flags & 3u) == 1u)
            break;
        if (n >= cap) {
            rc = GT2_ASSET_ERR_NOSPACE;
            goto done;
        }
        // Stem = name cut at the first '.' (game: strcpy to stack +
        // strchr('.') + NUL).
        char stem[GT2_VOL_NAME_MAX + 1];
        size_t k = 0;
        while (listing[i].name[k] && listing[i].name[k] != '.' &&
               k < sizeof stem - 1) {
            stem[k] = listing[i].name[k];
            k++;
        }
        stem[k] = '\0';
        out[n++] = gt2_crs_hash(stem);
        if (listing[i].flags & GT2_VOL_FLAG_END)
            break;
    }

done:
    if (rc == GT2_ASSET_OK) {
        if (count_out)
            *count_out = n;
        if (first_idx_out)
            *first_idx_out = first;
    }
    free(listing);
    return rc;
}

gt2_asset_status_t gt2_asset_window(const gt2_vol_t *vol, u32 tbl_idx,
                                    u8 **data_out, u32 *size_out) {
    if (!vol || !data_out)
        return GT2_ASSET_ERR_INVAL;
    *data_out = NULL;

    // Exact file range first (also validates the index).
    u32 off = 0, size = 0;
    gt2_vol_status_t st = gt2_vol_file_range(vol, tbl_idx, &off, &size);
    if (st != GT2_VOL_OK)
        return GT2_ASSET_ERR_INVAL;
    // Sector-aligned window: the game's reader works in whole sectors. The
    // read starts at start&~0x7FF (intra-sector offset dropped by the
    // >>11<<11 roundtrip in 0x8005D7D0) with length (next&~0x7FF) - start
    // (0x8005D848: aligned-next minus the EXACT start, so the window is
    // 0x3B short of a full sector span for tbl 8).
    u32 start_exact = off - GT2_VOL_BASE;
    u32 start = start_exact & ~0x7FFu;
    u32 end = (start_exact + size) & ~0x7FFu;
    if (end < start_exact)
        return GT2_ASSET_ERR_TRUNCATED;   // corrupt table, never wrap
    u32 len = end - start_exact;
    u8 *buf = malloc(len ? len : 1);
    if (!buf)
        return GT2_ASSET_ERR_NO_MEM;
    if (len > 0 &&
        gt2_vol_pread(vol, GT2_VOL_BASE + start, buf, len) != GT2_VOL_OK) {
        free(buf);
        return GT2_ASSET_ERR_IO;
    }
    *data_out = buf;
    if (size_out)
        *size_out = len;
    return GT2_ASSET_OK;
}

gt2_asset_status_t gt2_asset_window_cached(const gt2_vol_t *vol,
                                           const u16 *cache, u32 ncache,
                                           u32 slot, u8 **data_out,
                                           u32 *size_out) {
    // Mirrors 0x8005D8A0: tbl idx = u16 cache[slot].
    if (!vol || !cache || !data_out || slot >= ncache)
        return GT2_ASSET_ERR_INVAL;
    return gt2_asset_window(vol, cache[slot], data_out, size_out);
}

struct first_two {
    gt2_vol_entry_t e[2];
    u32 n;
};

static void first_two_cb(const gt2_vol_entry_t *e, void *ctx) {
    struct first_two *f = ctx;
    if (f->n < 2)
        f->e[f->n++] = *e;
}

gt2_asset_status_t gt2_q2_cache_build(const gt2_vol_t *vol,
                                      const char *const *paths, u32 npaths,
                                      u16 *cache_out) {
    // Mirrors 0x80010228: resolve each path via the tree walk; miss (or a
    // NULL path) pins 0xFFFF; files pin their tbl index; dirs pin the
    // first file's index (one slot past the '..' link, taken blindly).
    if (!vol || !cache_out || (npaths > 0 && !paths))
        return GT2_ASSET_ERR_INVAL;
    for (u32 i = 0; i < npaths; i++) {
        u16 pin = 0xFFFFu;
        if (paths[i] && paths[i][0]) {
            gt2_vol_entry_t e;
            if (gt2_vol_stat_path(vol, paths[i], &e) == GT2_VOL_OK) {
                if (e.flags & GT2_VOL_FLAG_DIR) {
                    struct first_two f = { { { 0 } }, 0 };
                    if (gt2_vol_list_dir(vol, paths[i], first_two_cb,
                                         &f) == GT2_VOL_OK && f.n == 2)
                        pin = (u16)f.e[1].next;
                } else {
                    pin = (u16)e.next;
                }
            }
        }
        cache_out[i] = pin;
    }
    return GT2_ASSET_OK;
}

gt2_asset_status_t gt2_crsinfo_parse(const u8 *data, u32 len,
                                     gt2_crs_rec_t *out, u32 cap,
                                     u32 *count_out) {
    if (!data || !out || cap == 0 || len < 8)
        return GT2_ASSET_ERR_INVAL;
    if (memcmp(data, GT2_CRS_MAGIC, 4) != 0)
        return GT2_ASSET_ERR_BAD_MAGIC;
    u32 count = (u32)data[6] | ((u32)data[7] << 8);   // u16 LE at +6
    // Overflow-safe bounds check: 8 + count*stride must fit the window.
    if (count > (len - 8) / GT2_CRS_REC_STRIDE)
        return GT2_ASSET_ERR_TRUNCATED;
    if (count > cap)
        return GT2_ASSET_ERR_NOSPACE;
    for (u32 i = 0; i < count; i++) {
        u32 b = 8 + i * GT2_CRS_REC_STRIDE;
        u32 off, hash;
        memcpy(&off, data + b, 4);
        memcpy(&hash, data + b + 4, 4);
        out[i].off = off;     // == emulated (rebased - load base)
        out[i].hash = hash;
    }
    if (count_out)
        *count_out = count;
    return GT2_ASSET_OK;
}

s32 gt2_crs_find(const gt2_crs_rec_t *recs, u32 count, u32 hash) {
    // Linear: both tables keep VOL listing order (unsorted), so ordering
    // assumptions would be wrong. Overlay-side consumer unmapped.
    if (!recs)
        return -1;
    for (u32 i = 0; i < count; i++) {
        if (recs[i].hash == hash)
            return (s32)i;
    }
    return -1;
}
