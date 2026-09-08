// GT2 native port: boot-task tails (task0b b2-b6).
// Mirrors SCUS 0x800104A0 (b3, five phases), 0x800107E8 (b4), 0x8001082C
// (b5), 0x8001083C (b6) plus the tiny callees 0x80010798 / 0x8005DD68 /
// 0x8005DE1C / 0x8005E07C / 0x800107B4. The game's 0x8008CE30 is a plain
// memset (emulation-proven zeroing); libc memset is used with the same
// (dst, value, len) shape. Verified by emulation; see gt2/task.h.

#include "gt2/task.h"

#include <stdlib.h>
#include <string.h>

const char *gt2_task_strerror(gt2_task_status_t st) {
    switch (st) {
    case GT2_TASK_OK: return "ok";
    case GT2_TASK_ERR_INVAL: return "invalid argument";
    case GT2_TASK_ERR_NO_MEM: return "out of memory";
    case GT2_TASK_ERR_IO: return "i/o error";
    case GT2_TASK_ERR_NOT_FOUND: return "not found";
    case GT2_TASK_ERR_NOSPACE: return "table full";
    case GT2_TASK_ERR_TRUNCATED: return "truncated data";
    case GT2_TASK_ERR_BAD_DATA: return "bad data";
    default: return "unknown";
    }
}

static void put_u16(u8 *p, u16 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static void put_u32(u8 *p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

gt2_task_status_t gt2_task_mark_init(u8 *p) {
    // 0x80010798: zero half at +0, -1 half at +0x4018, 0x2710 at +0x4014,
    // zero byte at +0x401B.
    if (!p)
        return GT2_TASK_ERR_INVAL;
    put_u16(p, 0);
    put_u16(p + 0x4018, 0xFFFFu);
    put_u32(p + 0x4014, 0x2710u);
    p[0x401B] = 0;
    return GT2_TASK_OK;
}

gt2_task_status_t gt2_task_slot_init(u8 *p) {
    // 0x8005DD68: five -1 words at +0..+0x10, then the low half of the
    // last word cleared (word at +0x10 reads back 0xFFFF0000).
    if (!p)
        return GT2_TASK_ERR_INVAL;
    for (u32 i = 0; i < 5; i++)
        put_u32(p + i * 4, 0xFFFFFFFFu);
    put_u16(p + 0x10, 0);
    return GT2_TASK_OK;
}

gt2_task_status_t gt2_task_block_init(u8 *p) {
    // 0x8005DE1C: three zero bytes, then 5 slot inits at +4+i*0x14 with a
    // zero byte at +0x68+i*0xC alongside each.
    if (!p)
        return GT2_TASK_ERR_INVAL;
    p[0] = p[1] = p[2] = 0;
    for (u32 i = 0; i < 5; i++) {
        gt2_task_slot_init(p + 4 + i * 0x14);
        p[0x68 + i * 0xC] = 0;
    }
    return GT2_TASK_OK;
}

gt2_task_status_t gt2_task_list_init(u8 *p) {
    // 0x8005E07C: 0xA4 clear, then eight -1 words at +8+i*0x14.
    if (!p)
        return GT2_TASK_ERR_INVAL;
    memset(p, 0, 0xA4);
    for (u32 i = 0; i < 8; i++)
        put_u32(p + 8 + i * 0x14, 0xFFFFFFFFu);
    return GT2_TASK_OK;
}

gt2_task_status_t gt2_task_big_init(u8 *p) {
    // 0x800107B4: 0x160 clear + u32 1 at +0x40.
    if (!p)
        return GT2_TASK_ERR_INVAL;
    memset(p, 0, 0x160);
    put_u32(p + 0x40, 1);
    return GT2_TASK_OK;
}

gt2_task_status_t gt2_task_b3_gather(u8 *base,
                                     const gt2_task_gather_src_t *src) {
    // Phase 1: 0xB6 clear, then GT2_TASK_GATHER_NRECS records. Each copies
    // the same five static spans (11/11/11/11/20 B); +0x2C..+0x3D of each
    // record stays zero.
    if (!base || !src || !src->b0 || !src->b1 || !src->b2 || !src->b3 ||
        !src->w)
        return GT2_TASK_ERR_INVAL;
    memset(base, 0, GT2_TASK_GATHER_LEN);
    for (u32 i = 0; i < GT2_TASK_GATHER_NRECS; i++) {
        u8 *r = base + 0xA + i * GT2_TASK_GATHER_REC;
        memcpy(r, src->b0, 11);
        memcpy(r + 0xB, src->b1, 11);
        memcpy(r + 0x16, src->b2, 11);
        memcpy(r + 0x21, src->b3, 11);
        memcpy(r + 0x3E, src->w, 20);
    }
    return GT2_TASK_OK;
}

gt2_task_status_t gt2_task_b3_marks(u8 *base) {
    // Phase 2: fixed bytes, then two mark inits 0x4028 apart.
    if (!base)
        return GT2_TASK_ERR_INVAL;
    base[0xB3] = 0xF0;
    base[0xB4] = 0xC0;
    base[0xB1] = base[0xB2] = base[0xB5] = 1;
    base[0] = 1;
    base[0xAE] = base[0xAF] = 0;
    base[2] = 0;
    base[3] = 2;
    base[4] = 1;
    base[5] = 0;
    base[6] = 2;
    base[8] = 1;
    gt2_task_mark_init(base + 0x3C74);
    gt2_task_mark_init(base + 0x3C74 + 0x4028);
    return GT2_TASK_OK;
}

gt2_task_status_t gt2_task_b3_slots(u8 *base, u32 count) {
    // Phase 3: count slot inits (game: CRS count from window+6).
    if (!base)
        return GT2_TASK_ERR_INVAL;
    for (u32 i = 0; i < count; i++)
        gt2_task_slot_init(base + 0x218 + i * GT2_TASK_SLOT_STRIDE);
    return GT2_TASK_OK;
}

gt2_task_status_t gt2_task_b3_blocks(u8 *base) {
    // Phase 4: 6 outer x 10 inner block inits.
    if (!base)
        return GT2_TASK_ERR_INVAL;
    for (u32 o = 0; o < GT2_TASK_BLOCK_OUTER; o++) {
        for (u32 i = 0; i < GT2_TASK_BLOCK_INNER; i++)
            gt2_task_block_init(base + 0x1418 + o * 0x668 + i * 0xA4);
    }
    return GT2_TASK_OK;
}

gt2_task_status_t gt2_task_b3_tails(u8 *base) {
    // Phase 5: three list inits + one big init.
    if (!base)
        return GT2_TASK_ERR_INVAL;
    gt2_task_list_init(base + 0x3A88);
    gt2_task_list_init(base + 0x3B2C);
    gt2_task_list_init(base + 0x3BD0);
    gt2_task_big_init(base + 0xB8);
    return GT2_TASK_OK;
}

gt2_task_status_t gt2_task_b4(u8 *dst, s16 *bounds) {
    // 0x40 clear + s16 pair (-0x40, +0x40).
    if (!dst || !bounds)
        return GT2_TASK_ERR_INVAL;
    memset(dst, 0, 0x40);
    bounds[0] = -0x40;
    bounds[1] = 0x40;
    return GT2_TASK_OK;
}

gt2_task_status_t gt2_task_b5(u8 *cell) {
    if (!cell)
        return GT2_TASK_ERR_INVAL;
    *cell = 0x60;
    return GT2_TASK_OK;
}

gt2_task_status_t gt2_task_b6(u8 *p) {
    // Exact 12-byte layout (holes at +3..+7 keep prior contents):
    // zero bytes +0..+2, zero word +8, -1 half +0xC, zero half +0xE,
    // zero byte +0x10.
    if (!p)
        return GT2_TASK_ERR_INVAL;
    p[0] = p[1] = p[2] = 0;
    put_u32(p + 8, 0);
    put_u16(p + 0xC, 0xFFFFu);
    put_u16(p + 0xE, 0);
    p[0x10] = 0;
    return GT2_TASK_OK;
}

static gt2_task_status_t map_car(gt2_car_status_t st) {
    switch (st) {
    case GT2_CAR_OK: return GT2_TASK_OK;
    case GT2_CAR_ERR_INVAL: return GT2_TASK_ERR_INVAL;
    case GT2_CAR_ERR_NOSPACE: return GT2_TASK_ERR_NOSPACE;
    case GT2_CAR_ERR_NOT_FOUND: return GT2_TASK_ERR_NOT_FOUND;
    case GT2_CAR_ERR_TRUNCATED: return GT2_TASK_ERR_TRUNCATED;
    default: return GT2_TASK_ERR_INVAL;
    }
}

static gt2_task_status_t map_asset(gt2_asset_status_t st) {
    switch (st) {
    case GT2_ASSET_OK: return GT2_TASK_OK;
    case GT2_ASSET_ERR_INVAL: return GT2_TASK_ERR_INVAL;
    case GT2_ASSET_ERR_NO_MEM: return GT2_TASK_ERR_NO_MEM;
    case GT2_ASSET_ERR_IO: return GT2_TASK_ERR_IO;
    case GT2_ASSET_ERR_BAD_MAGIC: return GT2_TASK_ERR_BAD_DATA;
    case GT2_ASSET_ERR_TRUNCATED: return GT2_TASK_ERR_TRUNCATED;
    case GT2_ASSET_ERR_NOSPACE: return GT2_TASK_ERR_NOSPACE;
    case GT2_ASSET_ERR_NOT_FOUND: return GT2_TASK_ERR_NOT_FOUND;
    default: return GT2_TASK_ERR_INVAL;
    }
}

static gt2_task_status_t map_vol(gt2_vol_status_t st) {
    switch (st) {
    case GT2_VOL_OK: return GT2_TASK_OK;
    case GT2_VOL_ERR_IO: return GT2_TASK_ERR_IO;
    case GT2_VOL_ERR_NO_MEM: return GT2_TASK_ERR_NO_MEM;
    case GT2_VOL_ERR_NOT_FOUND: return GT2_TASK_ERR_NOT_FOUND;
    case GT2_VOL_ERR_INVAL: return GT2_TASK_ERR_INVAL;
    case GT2_VOL_ERR_TRUNCATED: return GT2_TASK_ERR_TRUNCATED;
    default: return GT2_TASK_ERR_INVAL;
    }
}

gt2_task_status_t gt2_boot_state_create(gt2_boot_state_t **out) {
    if (!out)
        return GT2_TASK_ERR_INVAL;
    *out = NULL;
    gt2_boot_state_t *s = calloc(1, sizeof *s);
    if (!s)
        return GT2_TASK_ERR_NO_MEM;
    *out = s;
    return GT2_TASK_OK;
}

void gt2_boot_state_destroy(gt2_boot_state_t *s) {
    if (!s)
        return;
    free(s->crs_window);
    free(s);
}

gt2_task_status_t gt2_task_boot_run(gt2_boot_state_t *s,
                                    const gt2_vol_t *vol,
                                    const u8 weights[256],
                                    const gt2_task_gather_src_t *gather,
                                    const char *const *q2paths, u32 nq2) {
    // One deviation from hardware order: the Q2 cache is a VOL-init
    // effect (0x800102DC calls 0x80010228 before task0b), so it leads.
    if (!s || !vol || !weights || !gather || (nq2 > 0 && !q2paths) ||
        nq2 > GT2_BOOT_Q2_MAX || nq2 <= 6)
        return GT2_TASK_ERR_INVAL;
    free(s->crs_window);
    s->crs_window = NULL;
    gt2_task_status_t rc;
    gt2_car_status_t cr;
    gt2_asset_status_t ar;

    rc = map_asset(gt2_q2_cache_build(vol, q2paths, nq2, s->cache));
    if (rc != GT2_TASK_OK)
        return rc;
    for (u32 i = nq2; i < GT2_BOOT_Q2_MAX; i++)
        s->cache[i] = 0xFFFFu;

    // b0: car index + logo annotate.
    cr = gt2_car_index_build(vol, "/carobj", weights, s->cars,
                             GT2_CAR_MAX + 1, &s->car_count);
    if (map_car(cr) != GT2_TASK_OK)
        return map_car(cr);
    cr = gt2_car_logo_annotate(vol, weights, s->cars, s->car_count,
                               &s->logo_hits);
    if (map_car(cr) != GT2_TASK_OK)
        return map_car(cr);

    // b1: crsmap index + CRS window (cache slot 6) + parse.
    ar = gt2_crsmap_build(vol, s->crsmap, GT2_BOOT_CRSMAP_MAX,
                          &s->crsmap_count, &s->crsmap_first);
    if (map_asset(ar) != GT2_TASK_OK)
        return map_asset(ar);
    ar = gt2_asset_window_cached(vol, s->cache, GT2_BOOT_Q2_MAX, 6,
                                 &s->crs_window, &s->crs_window_len);
    if (map_asset(ar) != GT2_TASK_OK)
        return map_asset(ar);
    ar = gt2_crsinfo_parse(s->crs_window, s->crs_window_len, s->crs_recs,
                           GT2_BOOT_CRS_MAX, &s->crs_count);
    if (map_asset(ar) != GT2_TASK_OK)
        return map_asset(ar);

    // b2 intentionally skipped (CD-kick, HW-coupled; see task_notes.md).

    // b3: all five phases (slots take the parsed CRS count, as the game
    // reads it from window+6).
    rc = gt2_task_b3_gather(s->task_mem, gather);
    if (rc != GT2_TASK_OK)
        return rc;
    rc = gt2_task_b3_marks(s->task_mem);
    if (rc != GT2_TASK_OK)
        return rc;
    rc = gt2_task_b3_slots(s->task_mem, s->crs_count);
    if (rc != GT2_TASK_OK)
        return rc;
    rc = gt2_task_b3_blocks(s->task_mem);
    if (rc != GT2_TASK_OK)
        return rc;
    rc = gt2_task_b3_tails(s->task_mem);
    if (rc != GT2_TASK_OK)
        return rc;

    // b4/b5/b6.
    rc = gt2_task_b4(s->b4_area, s->bounds);
    if (rc != GT2_TASK_OK)
        return rc;
    rc = gt2_task_b5(&s->flag_cell);
    if (rc != GT2_TASK_OK)
        return rc;
    rc = gt2_task_b6(s->s6_area);
    if (rc != GT2_TASK_OK)
        return rc;

    // b7: replay span (game: cache[229]-cache[228]-1; the vol-span port
    // is the disc-side equivalent).
    rc = map_vol(gt2_vol_span(vol, "/replay/scea.000", "/replay/scea.999",
                              &s->span));
    return rc;
}
