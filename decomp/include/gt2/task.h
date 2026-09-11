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
// b2 (0x80011CE4) is ported as queue + complete (below): descriptor,
// heap bump and request formation are exact; the async DMA deposit lands
// via complete() after b7 (mirrors HW timing — the deposit range overlaps
// inputs b3..b7 still read). Full footprints in docs/task_notes.md.
//
// The runner below executes the whole boot chain in game order on native
// buffers (b2 queued, completed after b7): VOL-init cache -> b0 -> b1 ->
// b2(queue) -> b3 -> b4 -> b5 -> b6 -> b7 -> b2(complete). Game RAM
// addresses are caller buffers; the mapping is documented per field.

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

// --- b2 (0x80011CE4): sys.ins preload descriptor + queued transfer ---
// Game addresses: descriptor words at 0x801E2CE0 {dst, len, heap-tag},
// heap cell 0x80092E74 (SCUS-data init 4112), Q2 slot 247
// (/sound/sys.ins), VOL LBA cell 0x801C93E8 (473).
// Behavior (emulated end to end, see docs/task_notes.md): 0x80078790
// stores {0x801E2CF0, 0x200, heap}; the 0x800787CC/0x8006830C kick
// resolves Q2[247] (no SPU/GPU involvement), computes the CD request
// {LBA = lba_cell + (tbl[idx]>>11), len = (tbl[idx+1]&~0x7FF)-tbl[idx]}
// and queues an async DMA (HW leaves 0x8007CFDC/0x8007AB14/0x8007D024;
// completion is IRQ/polled, accredited to the SPU consumer path, NOT
// modeled here). The kick tail bumps the heap by ([DST+0x10]+0x1F)&-0x10
// (16 with zeroed buffers). Descriptor LEN (0x200) is the header extent
// the 0x80060884 consumer parses (0x40/0x7F units); the DMA carries the
// whole file. The deposit MUST NOT land before b3..b7 run (they read the
// Q2 cache + header copy the deposit range overlaps); complete() applies
// it after b7, mirroring the async timing.
#define GT2_B2_DESC_ADDR 0x801E2CE0u
#define GT2_B2_DST_ADDR 0x801E2CF0u
#define GT2_B2_DESC_LEN 0x200u
#define GT2_B2_HEAP_ADDR 0x80092E74u
#define GT2_B2_HEAP_INIT 4112u
#define GT2_B2_Q2SLOT 247u

typedef struct {
    u32 dst;       // transfer destination (GT2_B2_DST_ADDR analog)
    u32 desc_len;  // descriptor LEN (GT2_B2_DESC_LEN analog)
    u32 tag;       // heap watermark taken
    u32 tbl_idx;   // resolved Q2[247] file
    u32 lba;       // absolute CD LBA of the request
    u32 xfer_len;  // requested bytes (whole file from its start)
} gt2_task_b2_req_t;

// Queue the transfer: writes desc[3], bumps *heap_io by the exact
// formula, fills *req. dst16 = 16 B at DST+0x10 analog for the bump
// formula (NULL = zeros). q2 is the native Q2 cache (slot 247 resolves
// /sound/sys.ins); tbl comes from vol; lba_cell is the VOL LBA (473).
gt2_task_status_t gt2_task_b2_queue(const u16 *q2, u32 nq2,
                                    const gt2_vol_t *vol,
                                    u32 lba_cell, u32 *heap_io,
                                    const u8 *dst16,
                                    u32 desc[3], gt2_task_b2_req_t *req);
// Apply a queued transfer: deposits file[0:xfer_len) at dst (caller
// buffer, xfer_len bytes). file must hold the whole tbl_idx file.
gt2_task_status_t gt2_task_b2_complete(const gt2_task_b2_req_t *req,
                                       const u8 *file, u32 file_len,
                                       u8 *dst);

// --- native task0b runner (0x80010868 order) ---
// b2 is queued after b1 and completed after b7 (mirrors the async DMA:
// the deposit range overlaps cache/header inputs b3..b7 still read).
// Buffer <-> game-RAM map: cars = table at 0x801DF5D0 (+count cell),
// crsmap = 0x801E33F0, cache = 0x801E2EF0, crs_window = 0x801E18E0 load,
// task_mem = 0x801C98E0 area (0xC000 B covers the far mark init),
// b4_area = 0x801C98A0, bounds = static cell 0x800A6F18,
// flag_cell = byte 0x801C93C3, s6_area = 0x801EF5F0 struct,
// b2_desc = words at 0x801E2CE0, b2_heap = cell 0x80092E74 (disc init),
// b2_deposit = transfer target 0x801E2CF0 area (xfer_len bytes).

#define GT2_BOOT_Q2_MAX 248u
#define GT2_BOOT_TASK_MEM 0xC000u
#define GT2_BOOT_CRSMAP_MAX 256u
#define GT2_BOOT_CRS_MAX 256u
#define GT2_BOOT_B2_DEPOSIT_MAX 0x9000u   // sys.ins xfer (34600) fits

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
    u32 b2_desc[3];        // queued descriptor words
    u32 b2_heap;           // heap watermark after the kick bump
    gt2_task_b2_req_t b2_req;   // queued transfer
    u8 b2_deposit[GT2_BOOT_B2_DEPOSIT_MAX];   // completed transfer bytes
    u32 b2_deposit_len;
} gt2_boot_state_t;

gt2_task_status_t gt2_boot_state_create(gt2_boot_state_t **out);
void gt2_boot_state_destroy(gt2_boot_state_t *s);

// Full chain: VOL-init cache -> b0 (index+logo) -> b1 (crsmap+window+
// parse) -> b2 queue -> b3 (gather/marks/slots/blocks/tails) -> b4 ->
// b5 -> b6 -> b7 (span) -> b2 complete. `q2paths` (nq2 <= 248) is the
// boot path list; beyond nq2 the cache pins 0xFFFF. Slot 6 must be
// present (nq2 > 6) for the CRS window load, mirroring task0b1's
// hardcoded slot; slot 247 must be present for the b2 transfer.
gt2_task_status_t gt2_task_boot_run(gt2_boot_state_t *s,
                                    const gt2_vol_t *vol,
                                    const u8 weights[256],
                                    const gt2_task_gather_src_t *gather,
                                    const char *const *q2paths, u32 nq2);
