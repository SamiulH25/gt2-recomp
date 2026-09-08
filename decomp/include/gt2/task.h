#pragma once
// GT2 native port: boot-task tails (task0b's small initializers) + the
// native task0b runner.
//
// Task0b (SCUS 0x80010868) runs 8 tasks back to back; this module covers
// the tails (behavior reference confirmed by emulation, see
// docs/task_notes.md):
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
//
// The runner below executes the whole boot chain in game order on native
// buffers (b2 skipped, documented): VOL-init cache -> b0 -> b1 -> b3 ->
// b4 -> b5 -> b6 -> b7. Game RAM addresses are caller buffers; the
// mapping is documented per field.

#include "gt2/asset.h"
#include "gt2/car.h"
#include "gt2/task.h"
#include "gt2/vol.h"

typedef enum {
    GT2_TASK_OK = 0,
    GT2_TASK_ERR_INVAL,
    GT2_TASK_ERR_NO_MEM,
    GT2_TASK_ERR_IO,
    GT2_TASK_ERR_NOT_FOUND,
    GT2_TASK_ERR_NOSPACE,
    GT2_TASK_ERR_TRUNCATED,
    GT2_TASK_ERR_BAD_DATA,
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
// b6: 12-byte struct at p (game: 0x801EF5F0; holes +3..+7 kept).
gt2_task_status_t gt2_task_b6(u8 *p);

// --- native task0b runner (0x80010868 order) ---
// b2 is skipped (CD-kick, HW); everything else runs in game order.
// Buffer <-> game-RAM map: cars = table at 0x801DF5D0 (+count cell),
// crsmap = 0x801E33F0, cache = 0x801E2EF0, crs_window = 0x801E18E0 load,
// task_mem = 0x801C98E0 area (0xC000 B covers the far mark init),
// b4_area = 0x801C98A0, bounds = static cell 0x800A6F18,
// flag_cell = byte 0x801C93C3, s6_area = 0x801EF5F0 struct.

#define GT2_BOOT_Q2_MAX 248u
#define GT2_BOOT_TASK_MEM 0xC000u
#define GT2_BOOT_CRSMAP_MAX 256u
#define GT2_BOOT_CRS_MAX 256u

typedef struct {
    gt2_car_entry_t cars[GT2_CAR_MAX + 1];
    u32 car_count;
    u32 logo_hits;
    u32 crsmap[GT2_BOOT_CRSMAP_MAX];
    u32 crsmap_count;
    u32 crsmap_first;
    u16 cache[GT2_BOOT_Q2_MAX];
    u8 *crs_window;        // malloc'd by the run (asset window)
    u32 crs_window_len;
    gt2_crs_rec_t crs_recs[GT2_BOOT_CRS_MAX];
    u32 crs_count;
    u8 task_mem[GT2_BOOT_TASK_MEM];
    u8 b4_area[0x40];
    s16 bounds[2];
    u8 flag_cell;
    u8 s6_area[0x14];
    u32 span;              // b7 replay span
} gt2_boot_state_t;

gt2_task_status_t gt2_boot_state_create(gt2_boot_state_t **out);
void gt2_boot_state_destroy(gt2_boot_state_t *s);

// Full chain: VOL-init cache -> b0 (index+logo) -> b1 (crsmap+window+
// parse) -> b3 (gather/marks/slots/blocks/tails) -> b4 -> b5 -> b6 ->
// b7 (span). `q2paths` (nq2 <= 248) is the boot path list; beyond nq2
// the cache pins 0xFFFF. Slot 6 must be present (nq2 > 6) for the CRS
// window load, mirroring task0b1's hardcoded slot.
gt2_task_status_t gt2_task_boot_run(gt2_boot_state_t *s,
                                    const gt2_vol_t *vol,
                                    const u8 weights[256],
                                    const gt2_task_gather_src_t *gather,
                                    const char *const *q2paths, u32 nq2);
