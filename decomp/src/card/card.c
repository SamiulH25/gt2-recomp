// GT2 native port: card status driver (SCUS 0x80073720, 504 B).
// Clean-room C from emulated behavior; never pasted. See gt2/card.h +
// docs/save_notes.md.

#include "gt2/card.h"

#include <string.h>

const char *gt2_card_strerror(gt2_card_status_t st) {
    switch (st) {
    case GT2_CARD_OK: return "ok";
    case GT2_CARD_ERR_INVAL: return "invalid argument";
    case GT2_CARD_ERR_OOB: return "address outside data window";
    default: return "unknown";
    }
}

// --- little-endian field accessors (state is a raw game-layout block) ---

static u8 rd8(const u8 *p, u32 off) {
    return p[off];
}

static void wr8(u8 *p, u32 off, u8 v) {
    p[off] = v;
}

static s16 rd16(const u8 *p, u32 off) {
    s16 v;
    memcpy(&v, p + off, 2);
    return v;
}

static u16 rd16u(const u8 *p, u32 off) {
    u16 v;
    memcpy(&v, p + off, 2);
    return v;
}

static void wr16(u8 *p, u32 off, u16 v) {
    memcpy(p + off, &v, 2);
}

static u32 rd32(const u8 *p, u32 off) {
    u32 v;
    memcpy(&v, p + off, 4);
    return v;
}

// Saturating bump used by all three entry counters (wrapping u16
// increment, then clamp/zero at the limit — menu-counter family).
static void bump(u8 *state, u32 off, s16 lim, s16 reset) {
    u16 v = rd16u(state, off) + 1u;
    wr16(state, off, v);
    if ((s16)v >= lim)
        wr16(state, off, (u16)reset);
}

gt2_card_status_t gt2_card_run(u8 *state, const u8 *ev, u8 *data,
                               u32 data_len, const u8 *aux_tab,
                               u32 aux_len, const gt2_card_cb_t *cb,
                               void *ctx, int *rc_out) {
    s32 c;
    u32 s1, a1;
    if (!state || !data || !aux_tab || !cb || !rc_out || !cb->cfc4 ||
        !cb->be64 || !cb->da80 || !cb->c8fc4 || !cb->ad3c || !cb->d400 ||
        !cb->op3524 || !cb->op0840)
        return GT2_CARD_ERR_INVAL;

    cb->cfc4(state + 0x44, ctx);
    cb->be64(state + 0x28, ctx);
    // Entry counter +0x14: negative climbs (except -1, which exits).
    c = rd16(state, 0x14);
    if (c < 0) {
        if (c == -1) {
            *rc_out = -2;
            return GT2_CARD_OK;
        }
        wr16(state, 0x14, (u16)c + 1u);
        *rc_out = -2;
        return GT2_CARD_OK;
    }
    cb->da80(rd32(state, 0), ctx);
    bump(state, 0x14, 13, 12);
    bump(state, 0x16, 61, 0);
    bump(state, 0x18, 41, 0);
    if (!ev)
        goto exit_neg2;
    s1 = rd32(ev, 4);
    a1 = rd32(ev, 0xC);
    s1 |= a1;
    // 0x500 lane: +0x1C countdown.
    if (s1 & 0x500u) {
        c = rd16(state, 0x1C);
        if (c <= 0) {
            cb->op0840(0, ctx);
            goto exit_neg2;
        }
        cb->op0840(2, ctx);
        wr16(state, 0x1C, (u16)((u16)c - 1u));
        cb->op3524(data, (s16)((u16)c - 1u), ctx);
        goto exit_neg2;
    }
    // 0xA00 lane.
    if (s1 & 0xA00u) {
        if (rd8(state, 0x1A) != 0xCu) {
            int r = cb->c8fc4(data, ctx);
            if ((s32)r < (s32)rd16(state, 0x1E)) {
                // Byte-shift data[cnt1C..r] up by 1 (wrapping indices,
                // signed compare; game: memmove-down loop).
                s32 a0 = (s32)r;
                s32 cnt = rd16(state, 0x1C);
                s32 i;
                if (a0 >= cnt) {
                    if ((u64)(a0 + 1) >= data_len)
                        return GT2_CARD_ERR_OOB;
                    for (i = a0; i >= cnt; i--) {
                        if (i < 0 || (u64)i >= data_len)
                            return GT2_CARD_ERR_OOB;
                        data[i + 1] = data[i];
                    }
                }
                // Const-table byte into data[cnt].
                {
                    u32 idx = ((u32)rd8(state, 0x1A) << 4) +
                              rd8(state, 0x1B);
                    if (idx >= aux_len)
                        return GT2_CARD_ERR_OOB;
                    if ((s32)cnt < 0 || (u64)cnt >= data_len)
                        return GT2_CARD_ERR_OOB;
                    data[cnt] = aux_tab[idx];
                }
                {
                    int q = cb->ad3c(data, (s16)cnt, ctx);
                    if ((s32)q < (s32)rd16(state, 0x20)) {
                        // R5 entry: op(1), bump +0x1C, exit -2.
                        cb->op0840(1, ctx);
                        wr16(state, 0x1C,
                             rd16u(state, 0x1C) + 1u);
                        goto exit_neg2;
                    }
                    cb->op3524(data, rd16(state, 0x1C), ctx);
                    cb->op0840(0, ctx);
                    goto exit_neg2;
                }
            }
            cb->op0840(2, ctx);
            goto exit_neg2;
        }
        if (rd8(state, 0x1B) >= 8) {
            *rc_out = 0;
            return GT2_CARD_OK;
        }
        *rc_out = -1;
        return GT2_CARD_OK;
    }
    // Maze (0xA00 clear).
    if (s1 & 0x10000u) {
        cb->d400(state + 0x44, ctx);
        // DELAY SLOT: the sb fired with the 0xC arg before d400 ran;
        // d400's return is discarded (emu-proven).
        wr8(state, 0x1A, 0x0C);
        wr8(state, 0x1B, 8);
        cb->op0840(5, ctx);
        *rc_out = -3;
        return GT2_CARD_OK;
    }
    s1 |= a1;
    {
        u32 v0;
        // Countdown runs iff bit0x10 — but v0 is ALWAYS s1&0x1000 at
        // the join (both delay slots, 0x800739D0/EC, recompute it even
        // when their branches skip the countdown; emu-proven).
        if (s1 & 0x10u) {
            // 0x1000 countdown with zero clamp (wrapping u16 decrement,
            // then sign test — game: addiu/sll/bgez).
            s16 nc = (s16)(rd16u(state, 0x1C) - 1u);
            wr16(state, 0x1C, (u16)nc);
            if (nc < 0)
                wr16(state, 0x1C, 0);
        }
        v0 = s1 & 0x1000u;
        if (v0 != 0) {
            // Min-clamp +0x1C against c8fc4's return.
            u16 old = rd16u(state, 0x1C);
            int r = cb->c8fc4(data, ctx);
            wr16(state, 0x1C, old + 1u);
            if ((s32)r < (s32)(s16)(old + 1u))
                wr16(state, 0x1C, (u16)(u32)r);
        }
        if (v0 == 0)
            goto r8;
        // (v0 != 0 falls through to r8 with the clamped +0x1C)
    r8:
        wr8(state, 0x1A, (u8)rd16(state, 0x4A));
        if (s1 & 4u) {
            // Wrapping u8 decrement, then signed test (game: lbu/addiu
            // + sll-0x18/bgez pair).
            s8 b = (s8)(u8)(rd8(state, 0x1B) - 1u);
            wr8(state, 0x1B, (u8)b);
            if (b < 0)
                wr8(state, 0x1B, 0x0F);
        } else {
            goto r10;
        }
    r9b:
        if (rd8(state, 0x1A) != 0xCu)
            goto r10;
        wr8(state, 0x1B, 7);
        // falls through to the bit8 test
    r10: {
        u32 v0;
        if (!(s1 & 8u)) {
            // DELAY SLOT (0x80073A80): the beqz's delay recomputes v0 =
            // s1&0x101F whether or not it branches — R11 tests THAT, not
            // 0 (emu-proven; the fifth delay-slot catch this session).
            v0 = s1 & 0x101Fu;
            goto r11;
        }
        {
            s8 b = (s8)(u8)(rd8(state, 0x1B) + 1u);
            wr8(state, 0x1B, (u8)b);
            if (b >= 16)
                wr8(state, 0x1B, 0);
        }
        if (rd8(state, 0x1A) != 0xCu) {
            v0 = s1 & 0x101Fu;
        } else {
            wr8(state, 0x1B, 8);
            v0 = s1 & 0x101Fu;   // recomputed (game re-andis; the 8 in
                                 // v0 never survives — kept for mapping)
        }
    r11:
        if (v0 == 0)
            goto exit_neg2;
        cb->op0840(5, ctx);
        *rc_out = -3;
        return GT2_CARD_OK;
    }
    }   // end maze block
exit_neg2:
    *rc_out = -2;
    return GT2_CARD_OK;
}
