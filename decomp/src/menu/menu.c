// GT2 native port: menu-record lifecycle (gt2_02 0x80016394/0x800163C8/
// 0x80016410). Clean-room C from emulated behavior; never pasted.
// See gt2/menu.h + docs/menu_notes.md.

#include "gt2/menu.h"

#include <string.h>

#define GT2_MAGIC_DIV3 0x2AAAAAABu

const char *gt2_menu_strerror(gt2_menu_status_t st) {
    switch (st) {
    case GT2_MENU_OK: return "ok";
    case GT2_MENU_ERR_INVAL: return "invalid argument";
    default: return "unknown";
    }
}

gt2_menu_status_t gt2_menu_record_init(gt2_menu_rec_t *dst,
                                       const u8 tpl[16]) {
    if (!dst || !tpl)
        return GT2_MENU_ERR_INVAL;
    // Exact 12-insn field copy: holes (b1/h4/h6/+0x09..0x0B/list) kept.
    dst->b0 = tpl[0];
    memcpy(&dst->h2, tpl + 2, 2);
    dst->b8 = tpl[1];
    dst->cnt = -1;
    dst->x12 = 0x80u;
    return GT2_MENU_OK;
}

gt2_menu_status_t gt2_menu_counter_bump(gt2_menu_rec_t *rec) {
    if (!rec)
        return GT2_MENU_ERR_INVAL;
    s32 c = rec->cnt;
    if (c < 0) {
        // -1 sticks (idle); below -1 climbs one step toward -1.
        if (c < -1)
            rec->cnt = (s16)(c + 1);
    } else {
        // Wrapping increment, then saturate at 12 (32767 wraps to
        // -32768, which is < 12 and therefore kept as-is; only values
        // >= 12 clamp).
        s16 n = (s16)((u16)c + 1u);
        rec->cnt = (n < 12) ? n : 12;
    }
    return GT2_MENU_OK;
}

// --- exact-arithmetic helpers (mirror MIPS mult/mfhi/sra semantics) ---

// Low 32 of the product (same for signed/unsigned mult).
static s32 mullo(s32 a, s32 b) {
    return (s32)((u32)a * (u32)b);
}

// High 32 of the SIGNED 64-bit product (MIPS mult + mfhi).
static u32 mulhi(s32 a, s32 b) {
    return (u32)(((s64)a * (s64)b) >> 32);
}

// Arithmetic shift right by 1 (MIPS sra, sign-propagating).
static s32 sra1(u32 v) {
    return ((s32)v) >> 1;
}

// The div-3 idiom: mult x,0x2AAAAAAB; mfhi; sra 1; subu (x>>31).
static s32 div3(s32 x) {
    return sra1(mulhi(x, (s32)GT2_MAGIC_DIV3)) - (x >> 31);
}

// (s16)w arithmetically halved: sll 0x10 + sra 0x11.
static s32 shr1(s16 w) {
    return ((s32)w) >> 1;
}

// Compose an emit_A flag word: v | v<<8 | v<<16 | top.
static u32 compose(u32 v, u32 top) {
    v &= 0xFFu;
    return v | (v << 8) | (v << 16) | top;
}

// The 12-byte caller fill at an emitter-returned packet:
// word0 = (base - shr1(h4)) + ((s6 - shr1(h6)) << 16), then list w0/w4.
// All wrapping-u32 (game: subu/sll/addu chain).
static void fill_pkt(u8 *p, s32 base, s32 s6, const gt2_menu_list_t *list) {
    u32 lo = (u32)base - (u32)shr1((s16)list->h4);
    u32 hi = (u32)s6 - (u32)shr1((s16)list->h6);
    u32 w = lo + (hi << 16);
    memcpy(p, &w, 4);
    memcpy(p + 4, &list->w0, 4);
    memcpy(p + 8, &list->w4, 4);
}

u8 *gt2_menu_emit_tick(const gt2_menu_rec_t *rec, u8 *fp, u32 t4gate,
                       const gt2_menu_list_t *list,
                       gt2_menu_emit_fn emit_a, gt2_menu_emit_fn emit_b,
                       gt2_menu_short_fn emit_s, void *ctx) {
    if (!rec || !list || !emit_a || !emit_b || !emit_s)
        return NULL;
    u8 *last = NULL;

    // Idle check: sign-extended counter vs full list-address word.
    // (Effectively never equal in practice; ports the beq exactly.)
    if (rec->cnt == (s32)list->addr)
        return NULL;

    s32 s4 = rec->h4;
    s32 s6 = rec->h6;
    // t2 = ((u16)list.h4 << 16) >> 17 arithmetic: the sll-0x10/sra-0x11
    // pair, i.e. the sign-extended half, halved.
    s32 t2 = shr1((s16)list->h4);
    u32 b0 = rec->b0;
    if ((b0 & 0x60u) == 0x20u)
        s4 += t2;
    else if ((b0 & 0x60u) == 0x40u)
        s4 -= t2;

    s32 cnt = rec->cnt;
    if (cnt < 0) {
        // Fresh (-1) records rest; below -1 runs the short pair.
        if (!(cnt < -1))
            return NULL;
        s32 k = cnt + 13;
        s32 a2 = mullo((s16)list->h4, k);
        s32 t1 = mullo(rec->h2, k);
        s32 s2 = shr1((s16)list->h6);
        s32 a0 = mullo(s2, k);
        s32 t0 = rec->b8;
        s32 v1 = mullo(-t0, k);
        s32 s5 = div3(a2);
        s32 v = div3(a0);
        s32 na2 = s2 - v;
        s32 v2 = div3(v1);
        t0 = t0 + v2;
        s2 = t0 << 1;
        s32 s1 = div3(t1);
        if (!(s2 < 0xC1))
            s2 = 0xC0;
        s32 s0 = s4 - t2;
        gt2_menu_cell_t cell;
        cell.h10 = (s16)(s0 + s5);
        cell.h12 = (s16)(s6 - na2);
        cell.h14 = (s16)(((t2 << 1) + s1) - s5);
        cell.h16 = (s16)(na2 << 1);
        cell.w18 = 0;
        cell.w1c = (u32)s2;
        emit_s(fp, &cell, ctx);
        s0 = s0 - s1;
        cell.h10 = (s16)s0;
        cell.w18 = (u32)s2;
        cell.w1c = 0;
        emit_s(fp, &cell, ctx);
        last = emit_b(fp, 0x20u, ctx);
        return last;
    }

    // Blend (skipped at >= 12: s2 stays raw b8, s5 stays 1). Note a1
    // here still holds the counter (the 0x20 in the delay slot at
    // 0x800165F8 is short-path-only): the blend factor is (12 - cnt).
    s32 s2 = rec->b8;
    s32 s5 = 1;
    s32 a3v = 0;   // stale-zero on the skip path (game: leftover a3)
    if (cnt < 12) {
        s32 a0p = mullo(s2, cnt);
        s32 v1p = mullo(rec->h2, 12 - cnt);
        // srav-by-1 (t0 == 1 here) === sra-by-1 (inside div3).
        s2 = div3(a0p);
        s5 = div3(v1p);
        a3v = (rec->b8) - s2;
    }
    s32 t0 = (cnt < 12 && (b0 & 4u)) ? 1 : 0;
    s32 sv = (s16)rec->x12;   // lh sign-extends (matters at >= 0x8000)
    s32 m = mullo(s2, sv);
    u32 s7 = (b0 & 0x18u) << 2;
    s32 t4lo = m;
    s2 = t4lo >> 7;
    if (t0 != 0) {
        // First pair (emit_A twice with the a3-composed word).
        u32 s0c = compose((u32)a3v, 0x02000000u);
        u8 *p = emit_a(fp, s0c, ctx);
        last = p;
        fill_pkt(p, s4 - s5, s6, list);
        p = emit_a(fp, s0c, ctx);
        last = p;
        fill_pkt(p, s4 + s5, s6, list);
        emit_b(fp, list->h8 | s7, ctx);
    }
    if (!(b0 & 2u)) {
        // Shortcut stanza (no 0x2000000 top bit). Fill base is plain s4
        // (subu v1,s4,v1 at 0x80016934), unlike the first pair's s4-s5.
        u8 *p = emit_a(fp, compose((u32)s2, 0), ctx);
        last = p;
        fill_pkt(p, s4, s6, list);
    } else {
        // Full stanza: ONE emit_A on the s2-composed word (recomposed
        // here, not the a3 word), caller fill, emit_B.
        u8 *p = emit_a(fp, compose((u32)s2, 0x02000000u), ctx);
        last = p;
        fill_pkt(p, s4, s6, list);
        emit_b(fp, list->h8 | s7, ctx);
        if (b0 & 1u) {
            if (t4gate != 0) {
                p = emit_a(fp, 0x02000000u, ctx);
                last = p;
                fill_pkt(p, s4, s6, list);
                emit_b(fp, list->h8, ctx);
            } else {
                // Stale-free: the game reloads [sp+0x28] (always fresh
                // t4lo, delay-slot store) and re-derives s2 here.
                s2 = t4lo >> 8;
            }
            p = emit_a(fp, compose((u32)s2, 0x02000000u), ctx);
            last = p;
            fill_pkt(p, s4, s6, list);
            // Delay-slot OR: this B call sees h8|0x40, then flow JOINS
            // the tail (j 0x80016968) for a final h8|s7 emit. Both fire.
            emit_b(fp, list->h8 | 0x40u, ctx);
        }
    }
    last = emit_b(fp, list->h8 | s7, ctx);
    return last;
}
