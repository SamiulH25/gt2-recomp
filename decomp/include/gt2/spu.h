#pragma once
// GT2 native port: SPU voice shadow state.
//
// Behavior reference: SCUS 0x80078408 (init, confirmed by emulation) +
// 0x80078370 (spin until HW voices idle) + 0x800783DC (re-init gate).
// The game keeps 24 voice structs (0x28 stride) at RAM 0x801EFE68;
// hardware voice regs live at scratchpad 0x1F801C00 (24 x 0x10).

#include "gt2/types.h"

#define GT2_SPU_VOICES 24u
#define GT2_SPU_VOICE_STRIDE 0x28u
// Original RAM address (for notes/tools, not dereferenced by the port).
#define GT2_SPU_VOICE_TABLE 0x801EFE68u

typedef struct {
    u8 owner;       // +0x00: cleared on init; stale owner set to 0xFF
    u8 r01[3];      // +0x01..03: untouched by init (managed elsewhere)
    u8 f04;         // +0x04: cleared
    u8 mode;        // +0x05: set to 2
    u8 f06;         // +0x06: cleared
    u8 f07;         // +0x07: set to 1
    u32 f08;        // +0x08: cleared
    u8 data[8];     // +0x0C..13: untouched by init (pitch/vol/addrs)
    u32 link;       // +0x14: owner-link, cleared (old owner byte -> 0xFF)
    u8 tail[16];    // +0x18..27: untouched by init
} gt2_spu_voice_t;

// Reset all voices (clears link chain with 0xFF owner marks via `mark`;
// NULL mark only clears). Plain init skips marks.
typedef void (*gt2_spu_mark_fn)(u32 addr_cookie, void *ctx);
void gt2_spu_init_ex(gt2_spu_voice_t *voices, gt2_spu_mark_fn mark,
                     void *ctx);

static inline void gt2_spu_init(gt2_spu_voice_t *voices) {
    gt2_spu_init_ex(voices, NULL, NULL);
}

// Host predicate: game spins on HW regs until silent; natively voices
// start idle, so this is always true (kept for call-site parity).
static inline int gt2_spu_idle(const gt2_spu_voice_t *v) {
    (void)v;
    return 1;
}

// Spin until all voices read idle (mirrors SCUS 0x80078370, which polls
// the 24 HW voice regs at 0x1F801C00 [+0xC] until all zero or 0x675BFF
// spins elapse). Returns 1 if idle, 0 on timeout. The game calls this
// as a barrier in the loader path.
typedef int (*gt2_spu_busy_fn)(u32 voice, void *ctx);  // nonzero = busy
int gt2_spu_wait_idle(gt2_spu_busy_fn busy, void *ctx);

#define GT2_SPU_WAIT_LIMIT 0x675BFFu
