#pragma once
// GT2 native port: menu dispatcher cluster (gt2_01 overlay).
//
// gt2_01 `0x80017784` is a mode-indexed menu dispatcher (behavior
// reference confirmed by emulation, see docs/menu_notes.md and
// tools/menu_dispatch.py): guards on obj+0x5D0 and the task mode byte,
// then a 10-way indirect jump into TABLE1 (`0x8002F0B8`, runtime-filled:
// in-game those SCUS addresses hold overlay image bytes, so the table
// only exists after overlay self-registration writes it — same class as
// the `0x801EF610` dispatch fill). Arms A/B/C/D drive helpers and a
// mark-table-coupled tail; returns 2/4/7/9/11.
//
// Helpers: sync_0 (`0x8001710C`, pure task/object shuffle + stamp loop),
// wait0/wait1 (`0x80017174`/`0x800171B8`, set +0x38B then poll the
// `0x800833E8` virtual call until 0/1), sub-entry (`0x80017200`
// fallthrough arm only: worker `0x800472D4` + poll loop; the other 10
// TABLE2 arms need live table contents + W2 semantics — open).
// Workers `0x800472D4`/`0x8004DF34` are file-NOPs (runtime-patched), so
// all externals are injected callbacks, same pattern as gt2_menu emits.
//
// Task layout: taskbase (`0x801C98E0` analog) holds the mark area
// (+0x3C74); the menu-state block T2 = taskbase+`0xBF7C` holds the mode
// byte (+0xA), the tail halves (+0x582/+0x584) and writeback (+0x58).
// (An early analysis misread these as taskbase-relative; the slot watch
// + copy proof settled it — see menu_notes.md.)

#include "gt2/types.h"

typedef enum {
    GT2_DISPATCH_OK = 0,
    GT2_DISPATCH_ERR_INVAL,
    GT2_DISPATCH_ERR_UNKNOWN_ARM,   // table addr matches no known arm
                                    // (game would jr into it; native
                                    // refuses — documented deviation)
    GT2_DISPATCH_ERR_OOB,           // computed mark/stamp address outside
                                    // the task window (game reads wild
                                    // RAM; native refuses, cf. b2 miss)
} gt2_dispatch_status_t;

const char *gt2_dispatch_strerror(gt2_dispatch_status_t st);

#define GT2_DISPATCH_TASK_MIN 0xC000u   // covers T2 block + mark area
#define GT2_DISPATCH_OBJ_MIN 0x600u     // obj fields reach +0x5D8
#define GT2_DISPATCH_T2 0xBF7Cu         // menu-state block from taskbase
#define GT2_DISPATCH_TABLE1_N 10u
#define GT2_DISPATCH_TABLE2_N 11u

// Arm entry addresses (opaque table data values, gt2_01 VRAM).
#define GT2_DISPATCH_ARM_A_ADDR 0x800177E8u
#define GT2_DISPATCH_ARM_B_ADDR 0x8001782Cu
#define GT2_DISPATCH_ARM_C_ADDR 0x80017860u
#define GT2_DISPATCH_ARM_D_ADDR 0x800178C8u
// Sub fallthrough arm (only ported TABLE2 target).
#define GT2_DISPATCH_SUB_ENTRY_ADDR 0x80017254u
// Fallthrough worker data cookie (SCUS data pointer, opaque).
#define GT2_DISPATCH_SUB_DATA0 0x8005D348u

typedef enum {
    GT2_DISPATCH_ARM_A = 0,
    GT2_DISPATCH_ARM_B,
    GT2_DISPATCH_ARM_C,
    GT2_DISPATCH_ARM_D,
} gt2_dispatch_arm_t;

gt2_dispatch_status_t gt2_dispatch_arm_locate(u32 addr,
                                              gt2_dispatch_arm_t *arm_out);

typedef struct {
    // 0x800833E8 virtual-call result (game: obj->vtbl[3](obj)).
    int (*poll)(u8 *blk, void *ctx);
    // 0x800472D4 worker (blk = obj+0x38C analog, data = opaque cookie).
    void (*worker)(u8 *blk, u32 data, void *ctx);
} gt2_dispatch_cb_t;

// Guards + TABLE1 lookup: 0..9 on success, -1 when the guards fail
// (caller still runs the tail) or on null input.
int gt2_dispatch_select(const u8 *obj, const u8 *task,
                        const u32 table[GT2_DISPATCH_TABLE1_N]);

// One arm body; *rc_out is the game return code (2/4/7/9/11).
gt2_dispatch_status_t gt2_dispatch_arm_run(gt2_dispatch_arm_t arm, u8 *obj,
                                           u8 *task, u32 task_len,
                                           const gt2_dispatch_cb_t *cb,
                                           void *ctx, int *rc_out);

// Mark-table-coupled tail (halves gate + copy, +0x5D1 = 1); *rc_out = 9.
gt2_dispatch_status_t gt2_dispatch_tail(u8 *obj, u8 *task, u32 task_len,
                                        int *rc_out);

// Full run: select -> locate -> arm (unknown addr: UNKNOWN_ARM);
// guard-fail runs the tail only.
gt2_dispatch_status_t gt2_dispatch_run(u8 *obj, u8 *task, u32 task_len,
                                       const u32 table[GT2_DISPATCH_TABLE1_N],
                                       const gt2_dispatch_cb_t *cb,
                                       void *ctx, int *rc_out);

// Helpers (standalone).
gt2_dispatch_status_t gt2_dispatch_sync0(u8 *obj, u8 *task, u32 task_len,
                                         u32 v, u32 n);
gt2_dispatch_status_t gt2_dispatch_wait(u8 *obj, int flag,
                                        const gt2_dispatch_cb_t *cb,
                                        void *ctx);
// Sub fallthrough stanza (worker + poll loop); *rc_out = 1.
gt2_dispatch_status_t gt2_dispatch_sub_entry(u8 *obj, u32 data,
                                             const gt2_dispatch_cb_t *cb,
                                             void *ctx, int *rc_out);
// Sub TABLE2 lookup: 0..10 on success, -1 on guard fail / null.
int gt2_dispatch_sub_select(const u8 *task,
                            const u32 table[GT2_DISPATCH_TABLE2_N]);
