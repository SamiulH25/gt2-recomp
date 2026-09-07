// GT2 native port: GT2.OVL container parse + member inflate.
// Format measured from the US 1.2 sim disc; loader/decompressor behavior
// (staging 0x800A8D5C -> libpress gunzip -> VRAM 0x80010000) confirmed by
// emulation. Members inflate byte-exact to overlays/gt2_0{1..6}.exe.

#include "gt2/ovl.h"
#include "gt2/iso.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

struct gt2_ovl {
    gt2_cd_t *cd;       // borrowed (caller owns)
    u32 file_lba;       // GT2.OVL;1 extent
    u32 file_size;
    u32 comp[GT2_OVL_MEMBERS];
    u32 off[GT2_OVL_MEMBERS];   // member offsets from file start
};

const char *gt2_ovl_strerror(gt2_ovl_status_t st) {
    switch (st) {
    case GT2_OVL_OK: return "ok";
    case GT2_OVL_ERR_IO: return "i/o error";
    case GT2_OVL_ERR_NO_MEM: return "out of memory";
    case GT2_OVL_ERR_NOT_FOUND: return "GT2.OVL not found / bad header";
    case GT2_OVL_ERR_INVAL: return "invalid argument";
    case GT2_OVL_ERR_CORRUPT: return "corrupt gzip member";
    default: return "unknown";
    }
}

gt2_ovl_status_t gt2_ovl_open(gt2_cd_t *cd, gt2_ovl_t **out) {
    if (!cd || !out)
        return GT2_OVL_ERR_INVAL;
    *out = NULL;

    gt2_iso_entry_t e;
    if (gt2_iso_stat(cd, "GT2.OVL;1", &e) != GT2_ISO_OK)
        return GT2_OVL_ERR_NOT_FOUND;

    gt2_ovl_t *o = calloc(1, sizeof *o);
    if (!o)
        return GT2_OVL_ERR_NO_MEM;
    o->cd = cd;
    o->file_lba = e.extent_lba;
    o->file_size = e.size;

    // Header: u32 hdr_size, then per member comp_size; member offsets
    // for 1..5 (member 0 starts at hdr_size, member 5 runs to EOF).
    u8 hdr[48];
    u32 base = e.extent_lba * GT2_SECTOR_SIZE;
    if (gt2_cd_pread(cd, base, hdr, sizeof hdr) != GT2_CD_OK) {
        free(o);
        return GT2_OVL_ERR_IO;
    }
    u32 words[12];
    memcpy(words, hdr, sizeof words);
    if (words[0] == 0 || words[0] > o->file_size) {
        free(o);
        return GT2_OVL_ERR_NOT_FOUND;
    }
    o->off[0] = words[0];
    for (u32 i = 0; i < GT2_OVL_MEMBERS; i++)
        o->comp[i] = words[1 + 2 * i];
    for (u32 i = 1; i < GT2_OVL_MEMBERS; i++)
        o->off[i] = words[2 * i];
    // Sanity: strictly increasing offsets inside the file.
    for (u32 i = 0; i < GT2_OVL_MEMBERS; i++) {
        u32 end = (i + 1 < GT2_OVL_MEMBERS) ? o->off[i + 1] : o->file_size;
        if (o->off[i] < words[0] || o->comp[i] == 0 ||
            o->off[i] + o->comp[i] > end || end > o->file_size) {
            free(o);
            return GT2_OVL_ERR_NOT_FOUND;
        }
    }

    *out = o;
    return GT2_OVL_OK;
}

void gt2_ovl_close(gt2_ovl_t *ovl) {
    free(ovl);
}

u32 gt2_ovl_member_count(const gt2_ovl_t *ovl) {
    (void)ovl;
    return GT2_OVL_MEMBERS;
}

gt2_ovl_status_t gt2_ovl_member_range(const gt2_ovl_t *ovl, u32 idx,
                                      u32 *comp_off_out, u32 *comp_size_out) {
    if (!ovl || idx >= GT2_OVL_MEMBERS)
        return GT2_OVL_ERR_INVAL;
    if (comp_off_out)
        *comp_off_out = ovl->off[idx];
    if (comp_size_out)
        *comp_size_out = ovl->comp[idx];
    return GT2_OVL_OK;
}

gt2_ovl_status_t gt2_ovl_read_member(gt2_ovl_t *ovl, u32 idx,
                                     u8 **data_out, u32 *size_out) {
    if (!ovl || !data_out || idx >= GT2_OVL_MEMBERS)
        return GT2_OVL_ERR_INVAL;
    *data_out = NULL;

    u8 *comp = malloc(ovl->comp[idx]);
    if (!comp)
        return GT2_OVL_ERR_NO_MEM;
    u32 base = ovl->file_lba * GT2_SECTOR_SIZE;
    if (gt2_cd_pread(ovl->cd, base + ovl->off[idx], comp, ovl->comp[idx]) != GT2_CD_OK) {
        free(comp);
        return GT2_OVL_ERR_IO;
    }

    // Members are modest (<= 145KB comp, <= 317KB decomp); grow on demand.
    size_t cap = 512 * 1024;
    u8 *out = malloc(cap);
    if (!out) {
        free(comp);
        return GT2_OVL_ERR_NO_MEM;
    }
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (inflateInit2(&zs, 16 + 15) != Z_OK) {
        free(comp);
        free(out);
        return GT2_OVL_ERR_NO_MEM;
    }
    zs.next_in = comp;
    zs.avail_in = ovl->comp[idx];
    size_t total = 0;
    int rc = Z_OK;
    while (rc == Z_OK) {
        if (total == cap) {
            cap *= 2;
            u8 *grown = realloc(out, cap);
            if (!grown) {
                inflateEnd(&zs);
                free(comp);
                free(out);
                return GT2_OVL_ERR_NO_MEM;
            }
            out = grown;
        }
        zs.next_out = out + total;
        zs.avail_out = cap - total;
        rc = inflate(&zs, Z_NO_FLUSH);
        total = zs.total_out;
    }
    inflateEnd(&zs);
    free(comp);
    if (rc != Z_STREAM_END) {
        free(out);
        return GT2_OVL_ERR_CORRUPT;
    }
    *data_out = out;
    if (size_out)
        *size_out = (u32)total;
    return GT2_OVL_OK;
}
