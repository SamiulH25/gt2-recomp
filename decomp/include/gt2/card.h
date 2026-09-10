#pragma once
// GT2 native port: card status driver (SCUS 0x80073720, 504 B).
//
// Single-tick card operation runner (behavior reference confirmed by
// emulation, see docs/save_notes.md and tools/save_state.py): entry
// counter trio, event-struct dispatch (a1 = pointer; status @+4, extra
// @+0xC), the 0x500 countdown lane, the 0xA00 memmove/table lane, the
// bit maze (0x10000/0x10/0x1000/4/8/0x101F), returns -3/-2/-1/0.
//
// All eight callees are injected (card-coupled): cfc4/be64/da80 take
// state blocks, c8fc4/ad3c return scripted ints (their returns steer),
// d400's return is DISCARDED by a jal delay slot (the sb already fired
// with the 0xC arg — emu-proven), op3524/op0840 are effect sinks. The
// two sp+0x10 scratch blocks (7DA80/6AD3C args) are write-only in-game
// (no loads — verified by scan) and dropped from the signatures.
// +0x24 is a data-window pointer (never compared, only indexed): the
// caller provides the native bytes + length; the SCUS const table at
// 0x8009226C comes in as aux_tab (index (+0x1A<<4)+(+0x1B)).
// Out-of-window mark/stamp/table accesses refuse (game reads wild RAM;
// cf. b2 miss, dispatch OOB).

#include "gt2/types.h"

typedef enum {
    GT2_CARD_OK = 0,
    GT2_CARD_ERR_INVAL,
    GT2_CARD_ERR_OOB,
} gt2_card_status_t;

const char *gt2_card_strerror(gt2_card_status_t st);

#define GT2_CARD_STATE_MIN 0x50u   // fields reach +0x4A
#define GT2_CARD_EV_MIN 0x10u      // bits @+4/+0xC

typedef struct {
    void (*cfc4)(u8 *blk, void *ctx);          // 0x8006CFC4 (state+0x44)
    void (*be64)(u8 *blk, void *ctx);          // 0x8006BE64 (state+0x28)
    void (*da80)(u32 w, void *ctx);            // 0x8007DA80 (state[0])
    int (*c8fc4)(u8 *data, void *ctx);         // 0x8008CFC4 (scripted)
    int (*ad3c)(u8 *data, s16 n, void *ctx);   // 0x8006AD3C (scripted)
    void (*d400)(u8 *blk, void *ctx);          // 0x8006D400 (state+0x44)
    void (*op3524)(u8 *data, s16 v, void *ctx);// 0x80073524
    void (*op0840)(u32 v, void *ctx);          // 0x80060840 (op code)
} gt2_card_cb_t;

// One tick. state = 0x50+ B caller buffer, ev = 0x10 B event struct
// (or NULL → early -2 after the entry counters), data = +0x24 window,
// aux_tab = 0x8009226C-analog const window. *rc_out ∈ {-3,-2,-1,0}.
gt2_card_status_t gt2_card_run(u8 *state, const u8 *ev, u8 *data,
                               u32 data_len, const u8 *aux_tab,
                               u32 aux_len, const gt2_card_cb_t *cb,
                               void *ctx, int *rc_out);
