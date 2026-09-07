// GT2 native port: PSX memory-card image parser. Layout per
// decomp/docs + PSXSPX; verified against saves/card1.mcd (freshly
// formatted by the game: MC/0x0E header, 15x 0xA0 dir entries, empty
// broken list, FF data blocks).

#include "gt2/mcd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct gt2_mcd {
    u8 *img;    // owned copy, GT2_MCD_SIZE bytes
};

const char *gt2_mcd_strerror(gt2_mcd_status_t st) {
    switch (st) {
    case GT2_MCD_OK: return "ok";
    case GT2_MCD_ERR_IO: return "i/o error";
    case GT2_MCD_ERR_NO_MEM: return "out of memory";
    case GT2_MCD_ERR_INVAL: return "invalid image";
    case GT2_MCD_ERR_NOT_FOUND: return "not found";
    case GT2_MCD_ERR_NOSPACE: return "no space";
    case GT2_MCD_ERR_EXISTS: return "already exists";
    default: return "unknown";
    }
}

static u8 xor_sum(const u8 *p, u32 n) {
    u8 x = 0;
    for (u32 i = 0; i < n; i++)
        x ^= p[i];
    return x;
}

static const u8 *frame_at(const gt2_mcd_t *m, u32 block, u32 frame) {
    return m->img + (block * 64u + frame) * GT2_MCD_FRAME;
}

gt2_mcd_status_t gt2_mcd_open_mem(const u8 *data, u32 len, gt2_mcd_t **out) {
    if (!data || !out)
        return GT2_MCD_ERR_INVAL;
    *out = NULL;
    if (len != GT2_MCD_SIZE || data[0] != 'M' || data[1] != 'C')
        return GT2_MCD_ERR_INVAL;
    gt2_mcd_t *m = malloc(sizeof *m);
    if (!m)
        return GT2_MCD_ERR_NO_MEM;
    m->img = malloc(GT2_MCD_SIZE);
    if (!m->img) {
        free(m);
        return GT2_MCD_ERR_NO_MEM;
    }
    memcpy(m->img, data, GT2_MCD_SIZE);
    *out = m;
    return GT2_MCD_OK;
}

gt2_mcd_status_t gt2_mcd_open(const char *path, gt2_mcd_t **out) {
    if (!path || !out)
        return GT2_MCD_ERR_INVAL;
    *out = NULL;
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return GT2_MCD_ERR_IO;
    u8 *buf = malloc(GT2_MCD_SIZE);
    if (!buf) {
        fclose(fp);
        return GT2_MCD_ERR_NO_MEM;
    }
    size_t n = fread(buf, 1, GT2_MCD_SIZE, fp);
    fclose(fp);
    if (n != GT2_MCD_SIZE) {
        free(buf);
        return GT2_MCD_ERR_INVAL;
    }
    gt2_mcd_status_t st = gt2_mcd_open_mem(buf, GT2_MCD_SIZE, out);
    free(buf);
    return st;
}

void gt2_mcd_close(gt2_mcd_t *mcd) {
    if (!mcd)
        return;
    free(mcd->img);
    free(mcd);
}

int gt2_mcd_header_ok(const gt2_mcd_t *mcd) {
    if (!mcd)
        return 0;
    const u8 *h = frame_at(mcd, 0, 0);
    return h[0] == 'M' && h[1] == 'C' && h[0x7F] == xor_sum(h, 0x7F);
}

static int in_use(u32 state) {
    return state == GT2_MCD_ST_FIRST || state == GT2_MCD_ST_MID ||
           state == GT2_MCD_ST_LAST;
}

u32 gt2_mcd_file_count(const gt2_mcd_t *mcd) {
    if (!mcd)
        return 0;
    u32 n = 0;
    for (u32 i = 0; i < GT2_MCD_DIR_ENTRIES; i++) {
        const u8 *f = frame_at(mcd, 0, 1 + i);
        u32 st;
        memcpy(&st, f, 4);
        if (in_use(st))
            n++;
    }
    return n;
}

gt2_mcd_status_t gt2_mcd_dir(const gt2_mcd_t *mcd, u32 idx,
                             gt2_mcd_dir_t *out) {
    if (!mcd || !out || idx >= GT2_MCD_DIR_ENTRIES)
        return GT2_MCD_ERR_INVAL;
    const u8 *f = frame_at(mcd, 0, 1 + idx);
    memcpy(&out->state, f, 4);
    memcpy(&out->size, f + 4, 4);
    memcpy(&out->next, f + 8, 2);
    memcpy(out->name, f + 10, 21);
    out->name[21] = '\0';
    out->checksum_ok = (f[0x7F] == xor_sum(f, 0x7F));
    return GT2_MCD_OK;
}

gt2_mcd_status_t gt2_mcd_read(const gt2_mcd_t *mcd, u32 dir_idx,
                              u8 **data_out, u32 *size_out) {
    if (!mcd || !data_out || dir_idx >= GT2_MCD_DIR_ENTRIES)
        return GT2_MCD_ERR_INVAL;
    *data_out = NULL;
    gt2_mcd_dir_t d;
    gt2_mcd_status_t st = gt2_mcd_dir(mcd, dir_idx, &d);
    if (st != GT2_MCD_OK)
        return st;
    if (d.state != GT2_MCD_ST_FIRST)
        return GT2_MCD_ERR_NOT_FOUND;   // start of chain required
    u32 blocks = (d.size + 8191u) / 8192u;
    if (blocks == 0 || blocks > 15)
        return GT2_MCD_ERR_INVAL;
    u8 *buf = malloc(blocks * 8192u);
    if (!buf)
        return GT2_MCD_ERR_NO_MEM;
    // Dir frame i describes block i+1; continuations chain through
    // each block's own dir entry next field (block-1 based).
    u32 blk = dir_idx + 1u;  // 1..15
    for (u32 b = 0; b < blocks; b++) {
        if (blk < 1 || blk > 15) {
            free(buf);
            return GT2_MCD_ERR_INVAL;
        }
        memcpy(buf + b * 8192u, mcd->img + blk * 8192u, 8192u);
        if (b + 1 < blocks) {
            gt2_mcd_dir_t e;
            if (gt2_mcd_dir(mcd, blk - 1u, &e) != GT2_MCD_OK ||
                e.next == 0xFFFFu) {
                free(buf);
                return GT2_MCD_ERR_INVAL;
            }
            blk = (u32)e.next + 1u;
        }
    }
    *data_out = buf;
    if (size_out)
        *size_out = d.size;
    return GT2_MCD_OK;
}

const u8 *gt2_mcd_bytes(const gt2_mcd_t *mcd) {
    return mcd ? mcd->img : NULL;
}

static void frame_checksum(u8 *f) {
    f[0x7F] = xor_sum(f, 0x7F);
}

gt2_mcd_status_t gt2_mcd_format(u8 out[GT2_MCD_SIZE]) {
    if (!out)
        return GT2_MCD_ERR_INVAL;
    memset(out, 0, GT2_MCD_SIZE);
    // header + write-test copy
    out[0] = 'M';
    out[1] = 'C';
    frame_checksum(out);
    memcpy(out + 63u * 128u, out, 128);
    // directory: 15x free
    for (u32 i = 0; i < GT2_MCD_DIR_ENTRIES; i++) {
        u8 *f = out + (1u + i) * 128u;
        f[0] = (u8)GT2_MCD_ST_FREE;
        f[8] = 0xFF;
        f[9] = 0xFF;
        frame_checksum(f);
    }
    // broken list: none (this card keeps FFFF at [8..9] per frame)
    for (u32 i = 0; i < 20; i++) {
        u8 *f = out + (16u + i) * 128u;
        memset(f, 0, 128);
        memset(f, 0xFF, 4);
        f[8] = 0xFF;
        f[9] = 0xFF;
        frame_checksum(f);
    }
    // data blocks erased
    memset(out + 8192u, 0xFF, GT2_MCD_SIZE - 8192u);
    return GT2_MCD_OK;
}

// Block usage: block b (1..15) is used when some in-use entry chains
// through it.
static void used_blocks(const gt2_mcd_t *m, int used[16]) {
    for (int i = 0; i < 16; i++)
        used[i] = 0;
    used[0] = 1;    // directory
    for (u32 i = 0; i < GT2_MCD_DIR_ENTRIES; i++) {
        gt2_mcd_dir_t d;
        if (gt2_mcd_dir(m, i, &d) != GT2_MCD_OK || !in_use(d.state))
            continue;
        u32 blk = i + 1u;
        for (;;) {
            if (blk < 1 || blk > 15)
                break;
            used[blk] = 1;
            if (gt2_mcd_dir(m, blk - 1u, &d) != GT2_MCD_OK ||
                d.next == 0xFFFFu)
                break;
            blk = (u32)d.next + 1u;
        }
    }
}

gt2_mcd_status_t gt2_mcd_write(gt2_mcd_t *mcd, const char *name,
                               const u8 *data, u32 size) {
    if (!mcd || !name || !data || !name[0])
        return GT2_MCD_ERR_INVAL;
    size_t nl = strlen(name);
    if (nl > 20 || size == 0 || size % 8192u != 0 || size > 15u * 8192u)
        return GT2_MCD_ERR_INVAL;
    // duplicate?
    for (u32 i = 0; i < GT2_MCD_DIR_ENTRIES; i++) {
        gt2_mcd_dir_t d;
        if (gt2_mcd_dir(mcd, i, &d) == GT2_MCD_OK && in_use(d.state) &&
            !strcmp(d.name, name))
            return GT2_MCD_ERR_EXISTS;
    }
    u32 need = size / 8192u;
    int used[16];
    used_blocks(mcd, used);
    u32 pick[15];
    u32 got = 0;
    for (u32 b = 1; b <= 15 && got < need; b++) {
        if (!used[b])
            pick[got++] = b;
    }
    if (got < need)
        return GT2_MCD_ERR_NOSPACE;
    // free dir frames for the chain entries (fresh or deleted states)
    u32 frames[15];
    u32 nf = 0;
    for (u32 i = 0; i < GT2_MCD_DIR_ENTRIES && nf < need; i++) {
        gt2_mcd_dir_t d;
        if (gt2_mcd_dir(mcd, i, &d) == GT2_MCD_OK && !in_use(d.state))
            frames[nf++] = i;
    }
    if (nf < need)
        return GT2_MCD_ERR_NOSPACE;
    for (u32 k = 0; k < need; k++) {
        u8 *f = mcd->img + (1u + frames[k]) * 128u;
        memset(f, 0, 128);
        f[0] = (u8)(k == 0 ? GT2_MCD_ST_FIRST
                           : (k + 1 < need ? GT2_MCD_ST_MID : GT2_MCD_ST_LAST));
        memcpy(f + 4, &size, 4);
        u16 nx = (k + 1 < need) ? (u16)(pick[k + 1] - 1u) : 0xFFFFu;
        memcpy(f + 8, &nx, 2);
        if (k == 0) {
            memcpy(f + 10, name, nl + 1);
        }
        frame_checksum(f);
        memcpy(mcd->img + pick[k] * 8192u, data + k * 8192u, 8192u);
    }
    return GT2_MCD_OK;
}

gt2_mcd_status_t gt2_mcd_delete(gt2_mcd_t *mcd, u32 dir_idx) {
    if (!mcd || dir_idx >= GT2_MCD_DIR_ENTRIES)
        return GT2_MCD_ERR_INVAL;
    gt2_mcd_dir_t d;
    if (gt2_mcd_dir(mcd, dir_idx, &d) != GT2_MCD_OK || !in_use(d.state))
        return GT2_MCD_ERR_NOT_FOUND;
    // Mark the whole chain deleted (first/mid/last variants).
    u32 blk = dir_idx + 1u;
    int first = 1;
    for (;;) {
        if (blk < 1 || blk > 15)
            return GT2_MCD_ERR_INVAL;
        u8 *f = mcd->img + blk * 128u;   // dir frame == block number
        gt2_mcd_dir_t e;
        if (gt2_mcd_dir(mcd, blk - 1u, &e) != GT2_MCD_OK)
            return GT2_MCD_ERR_INVAL;
        u16 nx = e.next;
        f[0] = (u8)(first ? GT2_MCD_ST_DEL1
                          : (nx != 0xFFFFu ? GT2_MCD_ST_DEL2 : GT2_MCD_ST_DEL3));
        frame_checksum(f);
        first = 0;
        if (nx == 0xFFFFu)
            break;
        blk = (u32)nx + 1u;
    }
    return GT2_MCD_OK;
}
