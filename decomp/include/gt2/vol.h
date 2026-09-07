#pragma once
// GT2 native port: GT2.VOL (Polyphony "GTFS") read-only filesystem.
//
// Verified format (US 1.2 sim disc, VOL at LBA 473, all LE):
//   +0x00  u32  magic "GTFS" (0x53465447)
//   +0x04  u32  zero
//   +0x08  u16  data file count  (11581)
//   +0x0A  u16  entry count      (11620, hierarchical tree incl. dirs)
//   +0x0C  u32  reserved (zero)
//   +0x10  u32  tbl[data_count+1]: byte offsets of each file from VOL base.
//           file i lives at tbl[i], size tbl[i+1]-tbl[i]
//           (tbl[0] is 0x2FC: file 0 is the entry tree itself).
//   +0xBE7C records of 32 bytes: { u32 zero; u32 hash; u16 tbl_idx;
//           u8 pad; char name[21+NUL] }. 11568 records on this disc.
//           Runs until the first all-zero record. (34 records with >20-char
//           names have a nonzero first word and truncated names; idx is
//           valid — see decomp/docs/gtfs_notes.md Q9.)
//
// Provenance: offsets/sizes confirmed by extracting real files
// (arc_topmenu -> gzip, champtim.tim -> 16532 B). Behavior reference:
// SCUS 0x800100E4 (tree walk), 0x80010228 (index cache), 0x800102DC
// (init), 0x8005D74C (offset math), 0x8005D7D0 (sector read).
// See decomp/docs/gtfs_notes.md for open questions.

#include "gt2/types.h"

typedef struct gt2_vol gt2_vol_t;

typedef enum {
    GT2_VOL_OK = 0,
    GT2_VOL_ERR_IO,         // host I/O failure (fopen/fseek/fread)
    GT2_VOL_ERR_BAD_MAGIC,  // not a GTFS volume at VOL base
    GT2_VOL_ERR_TRUNCATED,  // image ends mid-table
    GT2_VOL_ERR_NO_MEM,
    GT2_VOL_ERR_NOT_FOUND,
    GT2_VOL_ERR_INVAL,      // bad argument / out-of-range index
    GT2_VOL_ERR_IS_DIR,     // path resolves to a directory, not a file
} gt2_vol_status_t;

// Open a raw 2352-byte dump or a cooked 2048-B/sector ISO (auto-detected,
// same rule as gt2_cd). The handle owns the image.
gt2_vol_status_t gt2_vol_open(const char *image_path, gt2_vol_t **out);
void gt2_vol_close(gt2_vol_t *vol);

// Header-declared data file count (11581 on US 1.2 sim).
u16 gt2_vol_file_count(const gt2_vol_t *vol);
// Number of parsed name records (11568 on US 1.2 sim).
u32 gt2_vol_name_count(const gt2_vol_t *vol);

// Byte range of data file `index` (0-based). `iso_off_out` is absolute in
// the ISO image; add nothing. Size may be 0 (empty files exist).
gt2_vol_status_t gt2_vol_file_range(const gt2_vol_t *vol, u32 index,
                                    u32 *iso_off_out, u32 *size_out);

// Flat lookup, e.g. "arc_topmenu", "champtim.tim".
gt2_vol_status_t gt2_vol_find(const gt2_vol_t *vol, const char *name,
                              u32 *iso_off_out, u32 *size_out);

// Read a whole named file; buffer is malloc'd, caller frees.
gt2_vol_status_t gt2_vol_read(gt2_vol_t *vol, const char *name,
                              u8 **data_out, u32 *size_out);

// Read a whole hierarchical path (final entry must be a file).
gt2_vol_status_t gt2_vol_read_path(gt2_vol_t *vol, const char *path,
                                   u8 **data_out, u32 *size_out);

typedef void (*gt2_vol_visit_fn)(const char *name, u32 index, void *ctx);
void gt2_vol_visit_names(const gt2_vol_t *vol, gt2_vol_visit_fn fn, void *ctx);

const char *gt2_vol_strerror(gt2_vol_status_t st);

// --- Hierarchical entry table (tree walk) ------------------------------
// On-disc, a single table of `entry_count` 32-byte slots at VOL+0xB800:
//   { u32 date (mastering timestamp, NOT a hash); u16 next; u8 flags;
//     char name[25+NUL] }. The flat name directory is the tail of this
//   same table (slots 52+ on US 1.2 sim), so both views agree.
// Behavior reference: SCUS search_vol_dir (0x800100E4), index cache
// builder (0x80010228). The game walks the table loaded in RAM starting
// at slot 0; directories resolve to their child-listing start slot.

#define GT2_VOL_SLOT_OFF 0xB800u
#define GT2_VOL_SLOT_REC 32u
#define GT2_VOL_FLAG_DIR 0x01u
#define GT2_VOL_FLAG_END 0x80u
#define GT2_VOL_NAME_MAX 25

typedef struct {
    u32 date;       // mastering timestamp (unix time)
    u32 slot;       // own slot position in the table
    u32 next;       // file: data-file index (tbl idx); dir: child start slot
    u8 flags;       // bit0 = directory, bit7 = end of listing
    char name[GT2_VOL_NAME_MAX + 1];
} gt2_vol_entry_t;

// Number of table slots (== header entry_count, 11620 on US 1.2 sim).
u32 gt2_vol_slot_count(const gt2_vol_t *vol);

// Resolve '/arcade/arc_carlogo' (leading '/' optional). Matches the game's
// walk: each component scans linearly from the current listing start until
// a name match or an END-flagged entry. Unlike the game (which descends
// blindly), a non-directory with remaining components is NOT_FOUND.
gt2_vol_status_t gt2_vol_stat_path(const gt2_vol_t *vol, const char *path,
                                   gt2_vol_entry_t *out);

// Data range for a hierarchical path (final entry must be a file).
gt2_vol_status_t gt2_vol_find_path(const gt2_vol_t *vol, const char *path,
                                   u32 *iso_off_out, u32 *size_out);

// Index span between two entries (mirrors task0b7 0x8001047C, which the
// game uses e.g. on the /replay/scea.* pair: count = idx(last) -
// idx(first) - 1 in u32 arithmetic, stored to RAM). Any entry kinds;
// missing paths are NOT_FOUND.
gt2_vol_status_t gt2_vol_span(const gt2_vol_t *vol, const char *first,
                              const char *last, u32 *count_out);

typedef void (*gt2_vol_entry_visit_fn)(const gt2_vol_entry_t *entry,
                                       void *ctx);
// Visit a directory listing ('/' or '' = root). Includes '..' links and
// the END-flagged last entry.
gt2_vol_status_t gt2_vol_list_dir(const gt2_vol_t *vol, const char *path,
                                  gt2_vol_entry_visit_fn fn, void *ctx);
