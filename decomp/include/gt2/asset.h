#pragma once
// GT2 native port: asset batch loading (course tables as the proven model).
//
// Behavior reference, all confirmed by emulation (see docs/asset_notes.md):
// - crs hash (SCUS 0x80083004): rol6-add over the NUL-terminated name:
//     v = rol(v, 6) + byte per char.
// - crsmap index (SCUS 0x80011B70, task0b1 head): walks the /crsmap listing
//   from the first file, strips the extension at the first '.', hashes the
//   stem, stores one u32 per file (120 entries at 0x801E33F0, count to
//   0x801C93E4, first tbl idx to 0x801C93DC).
// - asset window (SCUS 0x8005D848 + sector reader 0x8005D7D0): reads at
//   tbl[i]&~0x7FF with length (tbl[i+1]&~0x7FF) - tbl[i]. The data layout
//   follows the aligned base, not the exact file start (proven: the
//   .crsinfo CRS header sits at window+0, 0x3B before tbl[8]).
// - cache-slot load (SCUS 0x8005D8A0): tbl idx = u16 cache[slot] (Q2 index
//   cache at 0x801E2EF0; task0b1 uses slot 6 -> tbl 8 = /.crsinfo).
// - crsinfo relocate (SCUS 0x80011C70 tail): count = u16 at window+6 (126);
//   each record (stride 0x18 from window+8) rebases u32@+0 by the load base.
//   Record = {u32 target; u32 namehash; ...}; targets stay inside the window.
//   The port keeps offsets (target - window base); adding the host buffer
//   address is the native analog of the game's rebase.

#include "gt2/types.h"
#include "gt2/vol.h"

typedef enum {
    GT2_ASSET_OK = 0,
    GT2_ASSET_ERR_INVAL,      // bad argument / out-of-range index
    GT2_ASSET_ERR_NO_MEM,
    GT2_ASSET_ERR_IO,         // disc read failure
    GT2_ASSET_ERR_BAD_MAGIC,  // window is not a CRS blob
    GT2_ASSET_ERR_TRUNCATED,  // window ends mid-record
    GT2_ASSET_ERR_NOSPACE,    // caller table smaller than record count
    GT2_ASSET_ERR_NOT_FOUND,  // /crsmap (or path) missing
} gt2_asset_status_t;

const char *gt2_asset_strerror(gt2_asset_status_t st);

// rol6-add hash over a NUL-terminated name (empty -> 0).
u32 gt2_crs_hash(const char *name);

// Build the /crsmap hash table: every file from the first one on, stem
// (cut at first '.') hashed, in listing order. Writes up to `cap` u32s;
// count + first data-file (tbl) index via the out params (game: count to
// 0x801C93E4 = 120, first idx to 0x801C93DC = 8156 on US 1.2 sim).
gt2_asset_status_t gt2_crsmap_build(const gt2_vol_t *vol, u32 *out, u32 cap,
                                    u32 *count_out, u32 *first_idx_out);

// Load the aligned window for data-file `tbl_idx` (malloc'd, caller frees).
// Mirrors the game's sector-window load; `size_out` is the window length
// (0xFC5 for tbl 8 on US 1.2 sim).
gt2_asset_status_t gt2_asset_window(const gt2_vol_t *vol, u32 tbl_idx,
                                    u8 **data_out, u32 *size_out);

// Same, via a Q2 index-cache snapshot (u16 tbl indices, 0xFFFF = missing):
// tbl idx = cache[slot]. Mirrors 0x8005D8A0 (task0b1 passes slot 6).
gt2_asset_status_t gt2_asset_window_cached(const gt2_vol_t *vol,
                                           const u16 *cache, u32 ncache,
                                           u32 slot, u8 **data_out,
                                           u32 *size_out);

#define GT2_CRS_MAGIC "CRS\0"
#define GT2_CRS_REC_STRIDE 0x18u

typedef struct {
    u32 off;    // target offset from the window base (game: rebased pointer
                // minus load base; proven equal by emulation)
    u32 hash;   // course name hash (matches gt2_crsmap_build entries)
} gt2_crs_rec_t;

// Parse a loaded CRS window: checks the magic, bounds-checks count records,
// splits each into (offset, hash). The window bytes are NOT modified (the
// game rebases in place; native code adds its own base via gt2_crs_target).
gt2_asset_status_t gt2_crsinfo_parse(const u8 *data, u32 len,
                                     gt2_crs_rec_t *out, u32 cap,
                                     u32 *count_out);

// Native analog of a rebased record pointer.
static inline const u8 *gt2_crs_target(const u8 *base,
                                       const gt2_crs_rec_t *rec) {
    return base + rec->off;
}

// Linear scan by hash (both tables keep listing order, unsorted, so a
// binary search would be wrong). Returns the record index or -1.
s32 gt2_crs_find(const gt2_crs_rec_t *recs, u32 count, u32 hash);
