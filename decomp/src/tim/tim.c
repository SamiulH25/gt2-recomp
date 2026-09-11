// GT2 native port: PSX TIM decode/encode + logo locate + TXD strings.
// Written from the on-disc bytes (see gt2/tim.h); no game code involved.

#include "gt2/tim.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

const char *gt2_tim_strerror(gt2_tim_status_t st) {
    switch (st) {
    case GT2_TIM_OK: return "ok";
    case GT2_TIM_ERR_IO: return "i/o error";
    case GT2_TIM_ERR_MAGIC: return "bad TIM magic";
    case GT2_TIM_ERR_MODE: return "bad TIM mode";
    case GT2_TIM_ERR_TRUNCATED: return "truncated TIM";
    case GT2_TIM_ERR_NO_MEM: return "out of memory";
    case GT2_TIM_ERR_NOT_FOUND: return "not found";
    case GT2_TIM_ERR_INVAL: return "invalid argument";
    default: return "unknown";
    }
}

static u32 rd32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) |
           ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u16 rd16(const u8 *p) {
    return (u16)(p[0] | ((u16)p[1] << 8));
}

static void wr32(u8 *p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

// End offset of the TIM at off (headers only). *_ok gates strictness.
static gt2_tim_status_t tim_extent(const u8 *data, u32 len, u32 off,
                                   int allow_overshoot,
                                   gt2_tim_info_t *out) {
    if (off + 8 > len)
        return GT2_TIM_ERR_TRUNCATED;
    if (rd32(data + off) != 0x10)
        return GT2_TIM_ERR_MAGIC;
    u32 flag = rd32(data + off + 4);
    if (flag > 0x0F)
        return GT2_TIM_ERR_MODE;
    u32 mode = flag & 7;
    if (mode > 3)
        return GT2_TIM_ERR_MODE;
    u32 p = off + 8;
    gt2_tim_info_t info;
    memset(&info, 0, sizeof info);
    info.mode = mode;
    info.has_clut = (flag & 8) ? 1 : 0;
    if (flag & 8) {
        if (p + 12 > len)
            return GT2_TIM_ERR_TRUNCATED;
        u32 clen = rd32(data + p);
        if (clen < 12 || p + clen > len)
            return GT2_TIM_ERR_TRUNCATED;
        info.clut_x = rd16(data + p + 4);
        info.clut_y = rd16(data + p + 6);
        info.clut_w = rd16(data + p + 8);
        info.clut_h = rd16(data + p + 10);
        if (info.clut_w == 0 || info.clut_h == 0 ||
            info.clut_w > 256 || info.clut_h > 256)
            return GT2_TIM_ERR_MODE;
        info.clut_colors = (clen - 12) / 2;
        if (info.clut_colors == 0 || info.clut_colors > 256)
            return GT2_TIM_ERR_MODE;
        info.clut_off = p + 12;
        p += clen;
    }
    if (p + 12 > len)
        return GT2_TIM_ERR_TRUNCATED;
    u32 ilen = rd32(data + p);
    if (ilen < 12)
        return GT2_TIM_ERR_TRUNCATED;
    info.orgx = rd16(data + p + 4);
    info.orgy = rd16(data + p + 6);
    info.w = rd16(data + p + 8);
    info.h = rd16(data + p + 10);
    if (info.w == 0 || info.h == 0 || info.w > 1024 || info.h > 1024)
        return GT2_TIM_ERR_MODE;
    info.pix_off = p + 12;
    info.pix_len = ilen - 12;
    if (info.pix_off > len ||
        (!allow_overshoot && p + ilen > len))
        return GT2_TIM_ERR_TRUNCATED;
    // Upper bound sanity on the declared payload (catches flag confusion).
    if (ilen > (1u << 24))
        return GT2_TIM_ERR_TRUNCATED;
    info.total_len = (p - off) + ilen;
    if (out)
        *out = info;
    return GT2_TIM_OK;
}

gt2_tim_status_t gt2_tim_validate(const u8 *data, u32 len, u32 off,
                                  int allow_overshoot,
                                  gt2_tim_info_t *out) {
    if (!data || !out)
        return GT2_TIM_ERR_INVAL;
    if (off >= len)
        return GT2_TIM_ERR_TRUNCATED;
    return tim_extent(data, len, off, allow_overshoot, out);
}

gt2_tim_status_t gt2_tim_count(const u8 *data, u32 len, u32 *n_out) {
    if (!data || !n_out)
        return GT2_TIM_ERR_INVAL;
    u32 off = 0, n = 0;
    for (;;) {
        if (off >= len)
            break;
        gt2_tim_info_t info;
        if (tim_extent(data, len, off, 0, &info) != GT2_TIM_OK)
            break;   // tail is not a TIM: end of chain, not an error
        if (info.total_len == 0)
            break;
        off += info.total_len;
        n++;
    }
    *n_out = n;
    return GT2_TIM_OK;
}

gt2_tim_status_t gt2_tim_info(const u8 *data, u32 len, u32 index,
                              gt2_tim_info_t *out) {
    if (!data || !out)
        return GT2_TIM_ERR_INVAL;
    u32 off = 0;
    for (u32 i = 0;; i++) {
        gt2_tim_info_t info;
        // info() only addresses what count() found; anything else is
        // NOT_FOUND (chain walking itself uses tim_extent directly).
        gt2_tim_status_t st = tim_extent(data, len, off, 0, &info);
        if (st != GT2_TIM_OK)
            return GT2_TIM_ERR_NOT_FOUND;
        if (i == index) {
            *out = info;
            return GT2_TIM_OK;
        }
        off += info.total_len;
    }
}

u32 gt2_tim_pix_w(const gt2_tim_info_t *info) {
    if (!info)
        return 0;
    if (info->mode == 0)
        return info->w * 4;
    if (info->mode == 1)
        return info->w * 2;
    return info->w;
}

static void expand15(u16 v, u8 *rgb) {
    rgb[0] = (u8)(((v >> 0) & 31) * 255 / 31);
    rgb[1] = (u8)(((v >> 5) & 31) * 255 / 31);
    rgb[2] = (u8)(((v >> 10) & 31) * 255 / 31);
}

gt2_tim_status_t gt2_tim_decode(const u8 *data, u32 len, u32 index,
                                u8 **rgb_out, u32 *w_out, u32 *h_out) {
    if (!data || !rgb_out)
        return GT2_TIM_ERR_INVAL;
    *rgb_out = NULL;
    gt2_tim_info_t info;
    gt2_tim_status_t st = gt2_tim_info(data, len, index, &info);
    if (st != GT2_TIM_OK)
        return st;
    u32 w = gt2_tim_pix_w(&info), h = info.h;
    if (w == 0 || h == 0 || w > 4096 || h > 4096)
        return GT2_TIM_ERR_MODE;
    // Pixel bytes must actually be present (decode never overshoots,
    // even for logo containers whose headers declare past EOF).
    u64 need = 0;
    if (info.mode == 0)
        need = (u64)w * h / 2;
    else if (info.mode == 1)
        need = (u64)w * h;
    else if (info.mode == 2)
        need = (u64)w * h * 2;
    else
        need = (u64)w * h * 3;
    if ((u64)info.pix_off + need > (u64)len)
        return GT2_TIM_ERR_TRUNCATED;
    if (info.has_clut) {
        u64 clutneed = (u64)info.clut_colors * 2;
        if ((u64)info.clut_off + clutneed > (u64)len)
            return GT2_TIM_ERR_TRUNCATED;
    }
    u8 *rgb = malloc((size_t)w * h * 3);
    if (!rgb)
        return GT2_TIM_ERR_NO_MEM;
    const u8 *px = data + info.pix_off;
    if (info.mode == 2) {
        for (u32 i = 0; i < w * h; i++)
            expand15(rd16(px + i * 2), rgb + i * 3);
    } else if (info.mode == 3) {
        for (u32 i = 0; i < w * h; i++) {
            rgb[i * 3 + 0] = px[i * 3 + 2];
            rgb[i * 3 + 1] = px[i * 3 + 1];
            rgb[i * 3 + 2] = px[i * 3 + 0];
        }
    } else {
        // Indexed: build the CLUT once, then expand nibbles/bytes.
        u8 pal[256][3];
        for (u32 i = 0; i < info.clut_colors && i < 256; i++)
            expand15(rd16(data + info.clut_off + i * 2), pal[i]);
        if (info.mode == 0) {
            for (u32 i = 0; i < w * h; i++) {
                u32 palidx = (i & 1) ? (px[i / 2] >> 4) : (px[i / 2] & 15);
                if (palidx >= info.clut_colors)
                    palidx = 0;
                memcpy(rgb + i * 3, pal[palidx], 3);
            }
        } else {
            for (u32 i = 0; i < w * h; i++) {
                u32 palidx = px[i];
                if (palidx >= info.clut_colors)
                    palidx = 0;
                memcpy(rgb + i * 3, pal[palidx], 3);
            }
        }
    }
    *rgb_out = rgb;
    if (w_out)
        *w_out = w;
    if (h_out)
        *h_out = h;
    return GT2_TIM_OK;
}

gt2_tim_status_t gt2_tim_encode(const gt2_tim_info_t *info,
                                const u8 *clut, const u8 *pix,
                                u8 **out, u32 *len_out) {
    if (!info || !out)
        return GT2_TIM_ERR_INVAL;
    u32 flag = info->mode | (info->has_clut ? 8 : 0);
    if (info->mode > 3)
        return GT2_TIM_ERR_MODE;
    u32 clen = info->has_clut ? 12 + info->clut_colors * 2 : 0;
    u32 ilen = 12 + info->pix_len;
    u32 total = 8 + clen + ilen;
    u8 *b = malloc(total ? total : 1);
    if (!b)
        return GT2_TIM_ERR_NO_MEM;
    wr32(b, 0x10);
    wr32(b + 4, flag);
    u32 p = 8;
    if (info->has_clut) {
        if (!clut) {
            free(b);
            return GT2_TIM_ERR_INVAL;
        }
        wr32(b + p, clen);
        b[p + 4] = (u8)(info->clut_x & 0xFF);
        b[p + 5] = (u8)(info->clut_x >> 8);
        b[p + 6] = (u8)(info->clut_y & 0xFF);
        b[p + 7] = (u8)(info->clut_y >> 8);
        b[p + 8] = (u8)(info->clut_w & 0xFF);
        b[p + 9] = (u8)(info->clut_w >> 8);
        b[p + 10] = (u8)(info->clut_h & 0xFF);
        b[p + 11] = (u8)(info->clut_h >> 8);
        memcpy(b + p + 12, clut, info->clut_colors * 2);
        p += clen;
    }
    wr32(b + p, ilen);
    b[p + 4] = (u8)(info->orgx & 0xFF);
    b[p + 5] = (u8)(info->orgx >> 8);
    b[p + 6] = (u8)(info->orgy & 0xFF);
    b[p + 7] = (u8)(info->orgy >> 8);
    b[p + 8] = (u8)(info->w & 0xFF);
    b[p + 9] = (u8)(info->w >> 8);
    b[p + 10] = (u8)(info->h & 0xFF);
    b[p + 11] = (u8)(info->h >> 8);
    if (info->pix_len > 0) {
        if (!pix) {
            free(b);
            return GT2_TIM_ERR_INVAL;
        }
        memcpy(b + p + 12, pix, info->pix_len);
    }
    *out = b;
    if (len_out)
        *len_out = total;
    return GT2_TIM_OK;
}

gt2_tim_status_t gt2_logo_find_tim(const u8 *data, u32 len, u32 *off_out) {
    if (!data || !off_out)
        return GT2_TIM_ERR_INVAL;
    for (u32 off = 0; off + 20 <= len; off += 4) {
        gt2_tim_info_t info;
        if (tim_extent(data, len, off, 1, &info) != GT2_TIM_OK)
            continue;
        if (!info.has_clut)
            continue;
        *off_out = off;
        return GT2_TIM_OK;
    }
    return GT2_TIM_ERR_NOT_FOUND;
}

gt2_tim_status_t gt2_gunzip_join(const u8 *data, u32 len, u8 **out,
                                 u32 *len_out) {
    if (!data || !out)
        return GT2_TIM_ERR_INVAL;
    *out = NULL;
    u32 cap = 1u << 20;
    u8 *buf = malloc(cap);
    if (!buf)
        return GT2_TIM_ERR_NO_MEM;
    u32 done = 0, off = 0, members = 0;
    gt2_tim_status_t st = GT2_TIM_OK;
    while (off + 2 <= len) {
        if (members >= 4096) {
            st = GT2_TIM_ERR_TRUNCATED;
            break;
        }
        if (data[off] != 0x1F || data[off + 1] != 0x8B)
            break;   // padding/trailer: end of chain, not an error
        z_stream strm;
        memset(&strm, 0, sizeof strm);
        if (inflateInit2(&strm, MAX_WBITS + 32) != Z_OK) {
            st = GT2_TIM_ERR_NO_MEM;
            break;
        }
        strm.next_in = (Bytef *)data + off;
        strm.avail_in = len - off;
        int rok = 1;
        for (;;) {
            if (done >= cap) {
                cap *= 2;
                u8 *nb = realloc(buf, cap);
                if (!nb) {
                    rok = 0;
                    break;
                }
                buf = nb;
            }
            strm.next_out = buf + done;
            strm.avail_out = cap - done;
            int r = inflate(&strm, Z_NO_FLUSH);
            done = (u32)(strm.next_out - buf);
            if (r == Z_STREAM_END)
                break;
            if (r != Z_OK) {
                rok = 0;
                break;
            }
        }
        // next_in sits past the member trailer: advance to the next
        // 2048-aligned slot (inter-member padding).
        u32 consumed = (u32)(strm.next_in - data) - off;
        inflateEnd(&strm);
        if (!rok) {
            st = GT2_TIM_ERR_TRUNCATED;
            break;
        }
        members++;
        off += consumed;
        off = (off + 2047) & ~2047u;
    }
    if (st != GT2_TIM_OK || members == 0) {
        free(buf);
        return members == 0 ? GT2_TIM_ERR_TRUNCATED : st;
    }
    *out = buf;
    if (len_out)
        *len_out = done;
    return GT2_TIM_OK;
}

// Entries are non-empty NUL-terminated runs; padding NUL runs between
// strings carry no data and are skipped. (Whether the game's own reader
// counts padding as empty strings is overlay-consumer knowledge — open;
// what matters for mods is the span rule: a replacement must fit inside
// the original string's span, padded with NULs, so later offsets stay
// stable.)
u32 gt2_txd_count(const u8 *data, u32 len) {
    if (!data)
        return 0;
    u32 n = 0, p = 0;
    while (p < len) {
        while (p < len && data[p] == 0)
            p++;
        if (p >= len)
            break;
        u32 j = p;
        while (j < len && data[j] != 0)
            j++;
        n++;
        p = j + 1;
    }
    return n;
}

gt2_tim_status_t gt2_txd_get(const u8 *data, u32 len, u32 i,
                             const u8 **str_out, u32 *len_out) {
    if (!data || !str_out)
        return GT2_TIM_ERR_INVAL;
    u32 cur = 0, p = 0;
    while (p < len) {
        while (p < len && data[p] == 0)
            p++;
        if (p >= len)
            break;
        u32 j = p;
        while (j < len && data[j] != 0)
            j++;
        if (cur == i) {
            *str_out = data + p;
            if (len_out)
                *len_out = j - p;
            return GT2_TIM_OK;
        }
        cur++;
        p = j + 1;
    }
    return GT2_TIM_ERR_NOT_FOUND;
}
