#pragma once
// GT2 native port: boot-task tails (task0b's small initializers).
//
// Behavior reference, all confirmed by emulation (see docs/task_notes.md).
// Task0b (SCUS 0x80010868) runs 8 tasks; car_loader (b0) lives in
// gt2/car.h, crsmap/asset (b1) in gt2/asset.h, the span counter (b7) in
// gt2/vol.h. This module covers the tails:
//
// - b3 gather (0x800104A0 phase 1): memset 0xB6 at base, then 2 records
//   (stride 0x52 from base+0xA). Each record copies 11 B from each of 4
//   static byte tables plus 20 B from a static u16 table at +0x3E; the
//   +0x2C..+0x3D gap stays zero. Both records copy the SAME sources.
// - b3 marks (phase 2): fixed byte pattern at base+0x0..0x8 / 0xAE..0xB5,
//   plus two mark inits (0x80010798) at base+0x3C74 and base+0x7C9C.
// - b3 slots (phase 3): count x slot inits (0x8005DD68: five -1 words plus
//   a zero half at +0x10) at base+0x218+i*0x24. The game passes the CRS
//   count (126); the caller supplies it, keeping this decoupled.
// - b3 blocks (phase 4): 6x10 block inits (0x8005DE1C) at
//   base+0x1418+o*0x668+i*0xA4.
// - b3 tails (phase 5): three list inits (0x8005E07C: 0xA4 clear + eight
//   -1 words from +8 stride 0x14) at base+0x3A88/0x3B2C/0x3BD0, plus one
//   big init (0x800107B4: 0x160 clear + u32 1 at +0x40) at base+0xB8.
// - b4 (0x800107E8): 0x40 clear at dst + s16 bounds pair (-0x40, +0x40).
// - b5 (0x8001082C): one byte 0x60.
// - b6 (0x8001083C): 12-byte struct (zeros + 0xFFFF half at +0xC);
//   holes at +3..+7 are left untouched.
//
// b2 (0x80011CE4) is NOT ported: after a 3-word descriptor store it kicks
// a CD read through 0x800787CC/0x8006830C (HW-coupled, traps without the
// CD driver). Full footprints are in docs/task_notes.md.

#include "gt2/types.h"

typedef enum {
    GT2_TASK_OK = 0,
    GT2_TASK_ERR_INVAL,
} gt2_task_status_t;

const char *gt2_task_strerror(gt2_task_status_t st);

// Static sources for the b3 gather (game: SCUS rodata 0x80091570/7C/88/94
// and the u16 table at 0x800A6ED8; each read is 11/11/11/11/20 bytes).
typedef struct {
    const u8 *b0;
    const u8 *b1;
    const u8 *b2;
    const u8 *b3;
    const u8 *w;
} gt2_task_gather_src_t;

#define GT2_TASK_GATHER_LEN 0xB6u
#define GT2_TASK_GATHER_REC 0x52u
#define GT2_TASK_GATHER_NRECS 2u
#define GT2_TASK_SLOT_STRIDE 0x24u
#define GT2_TASK_BLOCK_OUTER 6u
#define GT2_TASK_BLOCK_INNER 10u

// Phase helpers (also the exact callees, made testable).
gt2_task_status_t gt2_task_mark_init(u8 *p);    // 0x80010798
gt2_task_status_t gt2_task_slot_init(u8 *p);    // 0x8005DD68
gt2_task_status_t gt2_task_block_init(u8 *p);   // 0x8005DE1C
gt2_task_status_t gt2_task_list_init(u8 *p);    // 0x8005E07C
gt2_task_status_t gt2_task_big_init(u8 *p);     // 0x800107B4

// Whole tasks. `base` is the caller's analog of RAM 0x801C98E0 (b3 needs
// ~0xBD00 bytes: the far mark init lands at +0x7C9C+0x4020).
gt2_task_status_t gt2_task_b3_gather(u8 *base,
                                     const gt2_task_gather_src_t *src);
gt2_task_status_t gt2_task_b3_marks(u8 *base);
gt2_task_status_t gt2_task_b3_slots(u8 *base, u32 count);
gt2_task_status_t gt2_task_b3_blocks(u8 *base);
gt2_task_status_t gt2_task_b3_tails(u8 *base);

// b4: 0x40 clear at dst; bounds pair written to `bounds` (game: the static
// cell 0x800A6F18, injected here instead of hardcoded).
gt2_task_status_t gt2_task_b4(u8 *dst, s16 *bounds);
// b5: *cell = 0x60 (game: byte at 0x801C93C3).
gt2_task_status_t gt2_task_b5(u8 *cell);
// b6: 0x11 clear at p + 0xFFFF half at +0xC (game: 0x801EF5F0).
gt2_task_status_t gt2_task_b6(u8 *p);
