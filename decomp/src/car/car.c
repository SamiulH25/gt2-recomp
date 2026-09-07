// GT2 native port: car index (name hash + flat table + lookup).
// Mirrors SCUS 0x80060924 (hash), 0x80011710 (carobj build),
// 0x8005D950 (binary search). Verified by emulation; see gt2/car.h.

#include "gt2/car.h"

#include <stdlib.h>
#include <string.h>

const char *gt2_car_strerror(gt2_car_status_t st) {
    switch (st) {
    case GT2_CAR_OK: return "ok";
    case GT2_CAR_ERR_INVAL: return "invalid argument";
    case GT2_CAR_ERR_NOSPACE: return "table full";
    case GT2_CAR_ERR_NOT_FOUND: return "directory not found";
    case GT2_CAR_ERR_TRUNCATED: return "table truncated";
    default: return "unknown";
    }
}

u32 gt2_namehash(const u8 weights[256], const char *name) {
    u8 c[5];
    size_t n = strlen(name);
    for (int i = 0; i < 5; i++)
        c[i] = i < (int)n ? (u8)name[i] : 0;
    u32 w0 = weights[c[0]], w1 = weights[c[1]], w2 = weights[c[2]];
    u32 w3 = weights[c[3]], w4 = weights[c[4]];
    return w4 | (w3 << 6) | (w2 << 12) | (w1 << 18) | (w0 << 24);
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

gt2_car_status_t gt2_car_index_build(const gt2_vol_t *vol, const char *dir,
                                     const u8 weights[256],
                                     gt2_car_entry_t *out, u32 cap,
                                     u32 *count_out) {
    if (!vol || !dir || !weights || !out || cap == 0)
        return GT2_CAR_ERR_INVAL;

    // Snapshot the directory listing (game scans raw slots; list_dir
    // visits the same range in the same order).
    gt2_vol_entry_t *listing = malloc(12288 * sizeof *listing);
    if (!listing)
        return GT2_CAR_ERR_NOSPACE;
    struct collect c = { listing, 0, 12288 };
    gt2_vol_status_t st = gt2_vol_list_dir(vol, dir, collect_cb, &c);
    if (st != GT2_VOL_OK) {
        free(listing);
        return st == GT2_VOL_ERR_NOT_FOUND ? GT2_CAR_ERR_NOT_FOUND
                                           : GT2_CAR_ERR_INVAL;
    }
    u32 nl = c.n;
    gt2_car_status_t rc = GT2_CAR_OK;
    u32 n = 0;

    // Skip phase: advance past leading dirs ('..'), landing on the
    // first file — mirrors the game's flag&3 skip loops (bounded here;
    // the game trusts the table).
    u32 i = 0;
    while (i < nl && (listing[i].flags & 3u) == 1u)
        i++;
    if (i >= nl) {
        rc = GT2_CAR_ERR_TRUNCATED;
        goto done;
    }
    for (; i < nl; i += 4) {
        if (n + 1 >= cap) {
            rc = GT2_CAR_ERR_NOSPACE;
            goto done;
        }
        out[n].hash = gt2_namehash(weights, listing[i].name);
        out[n].index = (u16)listing[i].next;
        out[n].z = 0;
        n++;
        // End when the 0x80 flag shows 3 slots ahead (game reads the
        // flags byte at entry+0x66).
        if (i + 3 >= nl || (listing[i + 3].flags & GT2_VOL_FLAG_END))
            break;
    }
    if (n == 0) {
        rc = GT2_CAR_OK;
        goto done;  // game would corrupt memory here; we return empty
    }
    // End sentinel one past the last entry (game writes next+4 there).
    if (n >= cap) {
        rc = GT2_CAR_ERR_NOSPACE;
        goto done;
    }
    out[n].hash = 0;
    out[n].index = (u16)(out[n - 1].index + 4);
    out[n].z = 0;

done:
    if (count_out && rc == GT2_CAR_OK)
        *count_out = n;
    free(listing);
    return rc;
}

s32 gt2_car_find(const gt2_car_entry_t *tab, u32 count, u32 hash) {
    if (!tab || count == 0)
        return -1;
    u32 lo = 0, hi = count;
    for (;;) {
        u32 mid = (lo + hi) >> 1;   // same midpoint as the game
        if (mid >= count)
            return -1;
        u32 h = tab[mid].hash;
        if (hash == h)
            return (s32)mid;
        if (!(lo < hi))
            return -1;  // game returns 0; port uses -1 (documented)
        if (h >= hash)
            hi = mid;
        else
            lo = mid + 1;
    }
}

u32 gt2_engine_parse(const char *name) {
    // Mirrors 0x80011670: leading decimal digits only.
    u32 v = 0;
    const u8 *p = (const u8 *)name;
    while (*p >= 0x30 && *p < 0x3A) {
        v = v * 10 + (u32)(*p - 0x30);
        p++;
    }
    return v;
}

u32 gt2_wheel_codec(const u8 *makers, u32 nmakers, const char *name) {
    // Maker index: first table entry with matching first two bytes
    // (game scans to the first zero entry; missing -> length).
    u32 idx = nmakers;
    if (makers && nmakers > 0 && !(makers[0] == 0 && makers[1] == 0)) {
        const s8 *n = (const s8 *)name;
        for (u32 i = 0; i < nmakers; i++) {
            u8 m0 = makers[2 * i], m1 = makers[2 * i + 1];
            if (m0 == 0 && m1 == 0)
                break;
            if ((u8)n[0] == m0 && (u8)n[1] == m1) {
                idx = i;
                break;
            }
            idx = i + 1;
        }
    } else if (makers) {
        idx = 0;
    }
    // Number + class with signed-byte arithmetic (game uses lb) and
    // full u32 wrap (no masks: negative intermediates keep high bits
    // through the shifts, matching sll/or chains bit-for-bit).
    const s8 *n = (const s8 *)name;
    s32 d2 = n[2] - 0x30, d3 = n[3] - 0x30;
    s32 num = 100 * d2 + 10 * d3 - 0x30 + n[4];
    s32 cls;
    if (n[6] == 0x35)
        cls = 2;
    else if (n[6] == 0x34)
        cls = 1;
    else if (n[6] == 0x36)
        cls = 3;
    else
        cls = 0;
    u32 a3 = (((idx << 12) | (u32)num) << 3) | (u32)cls;
    return (a3 << 13) | (u8)n[7];
}

gt2_car_status_t gt2_wheel_build(const gt2_vol_t *vol, const u8 *makers,
                                 u32 nmakers, u32 *out, u32 cap,
                                 u32 *count_out) {
    if (!vol || !out || cap == 0)
        return GT2_CAR_ERR_INVAL;
    gt2_vol_entry_t *listing = malloc(12288 * sizeof *listing);
    if (!listing)
        return GT2_CAR_ERR_NOSPACE;
    struct collect c = { listing, 0, 12288 };
    gt2_vol_status_t st = gt2_vol_list_dir(vol, "/carwheel", collect_cb, &c);
    if (st != GT2_VOL_OK) {
        free(listing);
        return st == GT2_VOL_ERR_NOT_FOUND ? GT2_CAR_ERR_NOT_FOUND
                                           : GT2_CAR_ERR_INVAL;
    }
    u32 nl = c.n, n = 0;
    gt2_car_status_t rc = GT2_CAR_OK;
    u32 i = 0;
    while (i < nl && (listing[i].flags & 3u) == 1u)
        i++;
    if (i >= nl) {
        rc = GT2_CAR_ERR_TRUNCATED;
        goto wdone;
    }
    // Stride-1 walk, end on the current entry's END flag (game reads
    // the flags byte just before the name pointer).
    for (; i < nl; i++) {
        if (n >= cap) {
            rc = GT2_CAR_ERR_NOSPACE;
            goto wdone;
        }
        out[n++] = gt2_wheel_codec(makers, nmakers, listing[i].name);
        if (listing[i].flags & GT2_VOL_FLAG_END)
            break;
    }
wdone:
    if (count_out && rc == GT2_CAR_OK)
        *count_out = n;
    free(listing);
    return rc;
}

gt2_car_status_t gt2_engine_build(const gt2_vol_t *vol, u16 *out, u32 cap,
                                  u32 *count_out) {
    if (!vol || !out || cap == 0)
        return GT2_CAR_ERR_INVAL;
    gt2_vol_entry_t *listing = malloc(12288 * sizeof *listing);
    if (!listing)
        return GT2_CAR_ERR_NOSPACE;
    struct collect c = { listing, 0, 12288 };
    gt2_vol_status_t st = gt2_vol_list_dir(vol, "/engine", collect_cb, &c);
    if (st != GT2_VOL_OK) {
        free(listing);
        return st == GT2_VOL_ERR_NOT_FOUND ? GT2_CAR_ERR_NOT_FOUND
                                           : GT2_CAR_ERR_INVAL;
    }
    u32 nl = c.n, n = 0;
    gt2_car_status_t rc = GT2_CAR_OK;
    u32 i = 0;
    while (i < nl && (listing[i].flags & 3u) == 1u)
        i++;
    if (i >= nl) {
        rc = GT2_CAR_ERR_TRUNCATED;
        goto edone;
    }
    // First name must start with a digit or the game returns without
    // writing the count (port returns empty instead of stale).
    if ((u8)listing[i].name[0] >= 0x3A)
        goto edone;
    // Stride-9 walk; ends when the entry 8 ahead carries END or the
    // next name doesn't start with a digit.
    for (; i < nl; i += 9) {
        if (n >= cap) {
            rc = GT2_CAR_ERR_NOSPACE;
            goto edone;
        }
        out[n++] = (u16)gt2_engine_parse(listing[i].name);
        if (i + 8 >= nl || (listing[i + 8].flags & GT2_VOL_FLAG_END))
            break;
        if (i + 9 >= nl || (u8)listing[i + 9].name[0] >= 0x3A)
            break;
    }
edone:
    if (count_out && rc == GT2_CAR_OK)
        *count_out = n;
    free(listing);
    return rc;
}
