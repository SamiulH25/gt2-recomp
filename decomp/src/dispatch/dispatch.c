// GT2 native port: menu dispatcher cluster (gt2_01 0x80017784 +
// helpers + 0x80017200 fallthrough). Clean-room C from emulated
// behavior; never pasted. See gt2/dispatch.h + docs/menu_notes.md.

#include "gt2/dispatch.h"

#include <string.h>

const char *gt2_dispatch_strerror(gt2_dispatch_status_t st) {
    switch (st) {
    case GT2_DISPATCH_OK: return "ok";
    case GT2_DISPATCH_ERR_INVAL: return "invalid argument";
    case GT2_DISPATCH_ERR_UNKNOWN_ARM: return "unknown arm address";
    case GT2_DISPATCH_ERR_OOB: return "address outside task window";
    default: return "unknown";
    }
}

gt2_dispatch_status_t gt2_dispatch_arm_locate(u32 addr,
                                              gt2_dispatch_arm_t *arm_out) {
    if (!arm_out)
        return GT2_DISPATCH_ERR_INVAL;
    switch (addr) {
    case GT2_DISPATCH_ARM_A_ADDR: *arm_out = GT2_DISPATCH_ARM_A; break;
    case GT2_DISPATCH_ARM_B_ADDR: *arm_out = GT2_DISPATCH_ARM_B; break;
    case GT2_DISPATCH_ARM_C_ADDR: *arm_out = GT2_DISPATCH_ARM_C; break;
    case GT2_DISPATCH_ARM_D_ADDR: *arm_out = GT2_DISPATCH_ARM_D; break;
    default: return GT2_DISPATCH_ERR_UNKNOWN_ARM;
    }
    return GT2_DISPATCH_OK;
}

// --- little-endian field accessors (game: lh/lbu/lw/sw/sh/sb) ---

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

static void wr16(u8 *p, u32 off, u16 v) {
    memcpy(p + off, &v, 2);
}

// T2-relative task access needs T2+off < len (len >= TASK_MIN covers the
// whole menu-state block, but the caller owns len).
static int t2_ok(u32 task_len, u32 off, u32 size) {
    u64 end = (u64)GT2_DISPATCH_T2 + off + size;
    return end <= task_len;
}

int gt2_dispatch_select(const u8 *obj, const u8 *task,
                        const u32 table[GT2_DISPATCH_TABLE1_N]) {
    if (!obj || !task || !table)
        return -1;
    // Guards: obj+0x5D0 nonzero, mode-1 < 10 (sltiu).
    if (obj[0x5D0] == 0)
        return -1;
    u32 mode = task[GT2_DISPATCH_T2 + 0xA];
    if (mode - 1u >= GT2_DISPATCH_TABLE1_N)
        return -1;
    (void)table;   // index only; the caller resolves table[index]
    return (int)(mode - 1u);
}

gt2_dispatch_status_t gt2_dispatch_sync0(u8 *obj, u8 *task, u32 task_len,
                                         u32 v, u32 n) {
    if (!obj || !task || task_len < GT2_DISPATCH_TASK_MIN)
        return GT2_DISPATCH_ERR_INVAL;
    wr16(obj, 0x5D8, (u16)v);
    wr8(task, GT2_DISPATCH_T2 + 0xA, (u8)n);
    wr8(task, GT2_DISPATCH_T2 + 0xD, (u8)rd16(obj, 0x5D6));
    wr8(task, GT2_DISPATCH_T2 + 0x5A, (u8)rd16(obj, 0x5D4));
    wr8(task, GT2_DISPATCH_T2 + 0xF, (u8)rd16(obj, 0x5D2));
    // Stamp loop: v1 = 0x5C stride 0xD0 while counter < (s16)obj+0x5D4
    // (SIGNED slt; negative N exits immediately).
    u32 v1 = 0x5Cu, a1 = 0;
    for (;;) {
        s16 lim = rd16(obj, 0x5D4);
        if (!((s32)a1 < (s32)lim))
            break;
        if (!t2_ok(task_len, v1 + 0x8Cu, 1))
            return GT2_DISPATCH_ERR_OOB;
        wr8(task, GT2_DISPATCH_T2 + v1 + 0x8C, 1);
        v1 += 0xD0u;
        a1++;
    }
    return GT2_DISPATCH_OK;
}

gt2_dispatch_status_t gt2_dispatch_wait(u8 *obj, int flag,
                                        const gt2_dispatch_cb_t *cb,
                                        void *ctx) {
    if (!obj || !cb || !cb->poll)
        return GT2_DISPATCH_ERR_INVAL;
    wr8(obj, 0x38B, flag ? 1u : 0u);
    // Poll until 0/1 (slti<2 pass, negative retries — game spins here).
    for (;;) {
        int v = cb->poll(obj + 0x304, ctx);
        if (v >= 0 && v < 2)
            break;
    }
    return GT2_DISPATCH_OK;
}

gt2_dispatch_status_t gt2_dispatch_sub_entry(u8 *obj, u32 data,
                                             const gt2_dispatch_cb_t *cb,
                                             void *ctx, int *rc_out) {
    if (!obj || !cb || !cb->poll || !cb->worker || !rc_out)
        return GT2_DISPATCH_ERR_INVAL;
    cb->worker(obj + 0x38C, data, ctx);
    for (;;) {
        int v = cb->poll(obj + 0x38C, ctx);
        if (v >= 0 && v < 2)
            break;
    }
    *rc_out = 1;   // game returns the last slti (always 1 here)
    return GT2_DISPATCH_OK;
}

int gt2_dispatch_sub_select(const u8 *task,
                            const u32 table[GT2_DISPATCH_TABLE2_N]) {
    if (!task || !table)
        return -1;
    u32 mode = task[GT2_DISPATCH_T2 + 0xA];
    if (mode - 1u >= GT2_DISPATCH_TABLE2_N)
        return -1;
    (void)table;
    return (int)(mode - 1u);
}

gt2_dispatch_status_t gt2_dispatch_tail(u8 *obj, u8 *task, u32 task_len,
                                        int *rc_out) {
    if (!obj || !task || !rc_out || task_len < GT2_DISPATCH_TASK_MIN)
        return GT2_DISPATCH_ERR_INVAL;
    s16 a0 = rd16(task, GT2_DISPATCH_T2 + 0x582);
    s16 a1 = rd16(task, GT2_DISPATCH_T2 + 0x584);
    // Copy iff both halves >= 0 (nor/srl pair per half, ANDed).
    if (a0 >= 0 && a1 >= 0) {
        u32 v0 = (u32)a0 * 16424u + 0x3C74u;   // wrapping (game: shifts)
        u32 ad = v0 + (u32)a1 * 164u;
        // Absolute from taskbase; +0xA6 read must land in-window.
        if ((u64)ad + 0xA8u > task_len)
            return GT2_DISPATCH_ERR_OOB;
        u16 val;
        memcpy(&val, task + ad + 0xA6u, 2);
        if (!t2_ok(task_len, 0x58u, 2))
            return GT2_DISPATCH_ERR_OOB;
        memcpy(task + GT2_DISPATCH_T2 + 0x58u, &val, 2);
    }
    wr8(obj, 0x5D1, 1);
    *rc_out = 9;
    return GT2_DISPATCH_OK;
}

// D-tail shared by arm B-else and arm D (wait1 + full tail).
static gt2_dispatch_status_t d_tail(u8 *obj, u8 *task, u32 task_len,
                                    const gt2_dispatch_cb_t *cb, void *ctx,
                                    int *rc_out) {
    gt2_dispatch_status_t st = gt2_dispatch_wait(obj, 1, cb, ctx);
    if (st != GT2_DISPATCH_OK)
        return st;
    return gt2_dispatch_tail(obj, task, task_len, rc_out);
}

gt2_dispatch_status_t gt2_dispatch_arm_run(gt2_dispatch_arm_t arm, u8 *obj,
                                           u8 *task, u32 task_len,
                                           const gt2_dispatch_cb_t *cb,
                                           void *ctx, int *rc_out) {
    gt2_dispatch_status_t st;
    int sub_rc;
    u8 flag;
    if (!obj || !task || !cb || !rc_out ||
        task_len < GT2_DISPATCH_TASK_MIN)
        return GT2_DISPATCH_ERR_INVAL;
    switch (arm) {
    case GT2_DISPATCH_ARM_A:
        // 17174 + 17200-entry + 171B8 (all three, unconditional).
        st = gt2_dispatch_wait(obj, 0, cb, ctx);
        if (st != GT2_DISPATCH_OK)
            return st;
        st = gt2_dispatch_sub_entry(obj, GT2_DISPATCH_SUB_DATA0, cb, ctx,
                                    &sub_rc);
        if (st != GT2_DISPATCH_OK)
            return st;
        st = gt2_dispatch_wait(obj, 1, cb, ctx);
        if (st != GT2_DISPATCH_OK)
            return st;
        flag = rd8(obj, 0x408);
        if (flag == 0) {
            *rc_out = 11;
            return GT2_DISPATCH_OK;
        }
        if (flag == 2) {
            *rc_out = 7;
            return GT2_DISPATCH_OK;
        }
        return gt2_dispatch_tail(obj, task, task_len, rc_out);
    case GT2_DISPATCH_ARM_B:
        st = gt2_dispatch_wait(obj, 0, cb, ctx);
        if (st != GT2_DISPATCH_OK)
            return st;
        st = gt2_dispatch_sub_entry(obj, GT2_DISPATCH_SUB_DATA0, cb, ctx,
                                    &sub_rc);
        if (st != GT2_DISPATCH_OK)
            return st;
        flag = rd8(obj, 0x408);
        if (flag == 0 || flag == 2) {
            // NOTE: flag==2 lands the epilogue via beq whose DELAY slot
            // already overwrote v0 with 7 — returns 7, not 2 (emu-proven).
            *rc_out = 7;
            return GT2_DISPATCH_OK;
        }
        return d_tail(obj, task, task_len, cb, ctx, rc_out);
    case GT2_DISPATCH_ARM_C: {
        st = gt2_dispatch_wait(obj, 0, cb, ctx);
        if (st != GT2_DISPATCH_OK)
            return st;
        st = gt2_dispatch_sub_entry(obj, GT2_DISPATCH_SUB_DATA0, cb, ctx,
                                    &sub_rc);
        if (st != GT2_DISPATCH_OK)
            return st;
        flag = rd8(obj, 0x408);
        if (flag == 2) {
            *rc_out = 7;
            return GT2_DISPATCH_OK;
        }
        if (flag == 0 || flag == 4) {
            // sync0(obj, mode-byte, 11); returns 4.
            u32 mode = task[GT2_DISPATCH_T2 + 0xA];
            st = gt2_dispatch_sync0(obj, task, task_len, mode, 11);
            if (st != GT2_DISPATCH_OK)
                return st;
            *rc_out = 4;
            return GT2_DISPATCH_OK;
        }
        return d_tail(obj, task, task_len, cb, ctx, rc_out);
    }
    case GT2_DISPATCH_ARM_D:
        return d_tail(obj, task, task_len, cb, ctx, rc_out);
    default:
        return GT2_DISPATCH_ERR_INVAL;
    }
}

gt2_dispatch_status_t gt2_dispatch_run(u8 *obj, u8 *task, u32 task_len,
                                       const u32 table[GT2_DISPATCH_TABLE1_N],
                                       const gt2_dispatch_cb_t *cb,
                                       void *ctx, int *rc_out) {
    gt2_dispatch_arm_t arm;
    int idx;
    if (!obj || !task || !table || !cb || !rc_out ||
        task_len < GT2_DISPATCH_TASK_MIN)
        return GT2_DISPATCH_ERR_INVAL;
    idx = gt2_dispatch_select(obj, task, table);
    if (idx < 0)
        return gt2_dispatch_tail(obj, task, task_len, rc_out);
    if (gt2_dispatch_arm_locate(table[idx], &arm) != GT2_DISPATCH_OK)
        return GT2_DISPATCH_ERR_UNKNOWN_ARM;
    return gt2_dispatch_arm_run(arm, obj, task, task_len, cb, ctx, rc_out);
}
