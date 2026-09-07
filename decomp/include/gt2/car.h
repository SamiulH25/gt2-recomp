#pragma once
// GT2 native port: car data index (name hash + per-dir tables).
//
// Behavior reference, all confirmed by emulation:
// - name hash (SCUS 0x80060924): 5-char suffix-weighted trie over a
//   256-byte weight table (RAM 0x801EF630, runtime data):
//     h = w[c4] | w[c3]<<6 | w[c2]<<12 | w[c1]<<18 | w[c0]<<24
//   (reads the name backwards — car names share prefixes).
// - carobj index (0x80011710): walks the /carobj listing from the first
//   .cdo entry with stride 4 (one per car: .cdo/.cdp/.cno/.cnp groups),
//   stores {u32 hash; u16 tbl_idx; u16 0} per car plus an end sentinel
//   (next = last.next+4) into the table at 0x801DF5D0, count (1110) to
//   0x801C93C8.
// - lookup (0x8005D950): binary search over that table by hash;
//   returns the entry or misses (port returns -1, game returns 0).

#include "gt2/types.h"
#include "gt2/vol.h"

// 5-char weighted hash. weights[256] as bytes (only low 6 bits used
// per level, matching the game's shifts).
u32 gt2_namehash(const u8 weights[256], const char *name);

#define GT2_CAR_MAX 2048u

typedef struct {
    u32 hash;
    u16 index;  // data-file (tbl) index
    u16 z;      // zero (sentinel layout parity with the game table)
} gt2_car_entry_t;

// Build the /carobj-style index: every 4th slot from the first .cdo
// entry (mirrors 0x80011710 skip/stride/end logic over `vol` slots).
// Writes up to `cap` entries + 1 sentinel; count via `count_out`.
typedef enum {
    GT2_CAR_OK = 0,
    GT2_CAR_ERR_INVAL,
    GT2_CAR_ERR_NOSPACE,
    GT2_CAR_ERR_NOT_FOUND,  // /carobj missing
    GT2_CAR_ERR_TRUNCATED,  // malformed table (game would overrun)
} gt2_car_status_t;

gt2_car_status_t gt2_car_index_build(const gt2_vol_t *vol, const char *dir,
                                     const u8 weights[256],
                                     gt2_car_entry_t *out, u32 cap,
                                     u32 *count_out);

// Binary search by hash (mirrors 0x8005D950). Returns entry index or -1.
s32 gt2_car_find(const gt2_car_entry_t *tab, u32 count, u32 hash);

// --- wheel codec + tables (carwheel_loader 0x8001194C) ---
// Packs maker/index/number/class/name[7] as the game does:
//   v0 = maker<<28 | num<<16 | cls<<13 | name[7]
// with num = 100*d2+10*d3+d4' (signed-byte arithmetic, d4' raw),
// cls from name[6] ('4'->1,'5'->2,'6'->3, else 0). makers is a flat
// array of 2-char codes (default game table: "bbbrduenfaozraspyo").
// Missing maker yields index = nmakers (game scans to zeros).
u32 gt2_wheel_codec(const u8 *makers, u32 nmakers, const char *name);

// Build the /carwheel u32 table (stride-1 walk, end on flags&0x80).
gt2_car_status_t gt2_wheel_build(const gt2_vol_t *vol, const u8 *makers,
                                 u32 nmakers, u32 *out, u32 cap,
                                 u32 *count_out);

// --- engine sounds (enginedata_loader 0x80011A10) ---
// Leading-decimal parse (mirrors 0x80011670).
u32 gt2_engine_parse(const char *name);

// Build the /engine u16 table (stride-9 walk, dual end conditions).
gt2_car_status_t gt2_engine_build(const gt2_vol_t *vol, u16 *out, u32 cap,
                                  u32 *count_out);

const char *gt2_car_strerror(gt2_car_status_t st);
