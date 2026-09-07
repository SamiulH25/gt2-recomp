// GT2 native port: SPU voice init. Mirrors SCUS 0x80078408 as observed
// in emulation (24 x 0x28 entries; stale links unlinked with an 0xFF
// owner mark; the trailing 0x8007916c call and the 0x80092E88 global
// are SPU-HW concerns left to the host audio backend).
//
// Link cookies: in the game +0x14 holds a RAM address whose byte is
// marked 0xFF on unlink. Natively there is no guest RAM, so the mark
// goes through a callback (NULL = clear the link, skip the mark).

#include "gt2/spu.h"

#include <stddef.h>

void gt2_spu_init_ex(gt2_spu_voice_t *voices, gt2_spu_mark_fn mark,
                     void *ctx) {
    if (!voices)
        return;
    for (u32 i = 0; i < GT2_SPU_VOICES; i++) {
        gt2_spu_voice_t *v = &voices[i];
        if (v->link != 0) {
            if (mark)
                mark(v->link, ctx);
            v->link = 0;
        }
        v->owner = 0;
        v->f04 = 0;
        v->mode = 2;
        v->f06 = 0;
        v->f07 = 1;
        v->f08 = 0;
    }
}

int gt2_spu_wait_idle(gt2_spu_busy_fn busy, void *ctx) {
    if (!busy)
        return 1;
    for (u32 spin = 0;; spin++) {
        u32 any = 0;
        for (u32 i = 0; i < GT2_SPU_VOICES; i++) {
            if (busy(i, ctx)) {
                any = 1;
                break;
            }
        }
        if (!any)
            return 1;
        if (spin >= GT2_SPU_WAIT_LIMIT)
            return 0;
    }
}
