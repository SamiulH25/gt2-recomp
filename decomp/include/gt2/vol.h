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

// Raw offset-table entry tbl[i] (VOL-relative byte offset, 0 <= i <=
// file_count). Exposes the degenerate final marker as-is (0 on this
// disc); the game does its own wrapping arithmetic on these values.
gt2_vol_status_t gt2_vol_tbl_entry(const gt2_vol_t *vol, u32 i, u32 *off_out);

// Raw byte read at an absolute cooked-space image offset (e.g. from
// gt2_vol_file_range). Used for sector-aligned window loads that do not
// coincide with exact file ranges (see gt2/asset.h).
gt2_vol_status_t gt2_vol_pread(const gt2_vol_t *vol, u32 abs_off, void *buf,
                               u32 len);

typedef void (*gt2_vol_visit_fn)(const char *name, u32 index, void *ctx);
void gt2_vol_visit_names(const gt2_vol_t *vol, gt2_vol_visit_fn fn, void *ctx);

// Open a VOL extent blob from memory (same layout as the on-disc extent
// starting at the GTFS magic, e.g. produced by gt2_vol_pack). The handle
// borrows `blob` (must outlive the handle); tables are still parsed and
// owned. Size must cover the header + offset table at minimum.
//
// NOTE: gt2_vol_file_range/find return offsets with the GT2_VOL_BASE bias
// (image-absolute, matching the cd backend). For mem handles subtract
// GT2_VOL_BASE to index the blob. gt2_vol_pread hides this (it takes the
// same absolute form for both backends).
gt2_vol_status_t gt2_vol_open_mem(const u8 *blob, u32 size, gt2_vol_t **out);

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

// --- Repack (VOL extent rebuild) --------------------------------------
// On-disc constraints (US 1.2 sim, verified by probing the image):
//   - tbl[] holds plain byte offsets, essentially unaligned (3905/11582
//     are not even 4-aligned), so files pack tightly with no padding.
//   - Files 0 ([0x2FC,0xBB80)) and 1 ([0xBB80,0x66B22)) carry the tables:
//     the slot tree overlaps file 0's tail and file 1's head, and the
//     flat dir sits inside file 1. File 1 is preserved byte-exact; file 0
//     keeps everything except the embedded offset array itself, which
//     spans [0x10, 0x10+4*(data_count+1)) = [0x10,0xB508) and therefore
//     overlaps file 0 over [0x2FC,0xB488) plus its last-32 tail at
//     [0xB488,0xB508) — all restamped, none mirrored (there is no second
//     copy). The slot `next` fields and flat `tbl_idx` fields name FILES,
//     not offsets, so they stay valid with no table surgery.
//   - The final marker tbl[data_count] is 0 (file data_count-1 is
//     degenerate with no valid range); it is preserved as-is.
// New files keep index order; offsets are recomputed from tbl[2] onward;
// the blob covers [0, end of file data_count-2).
typedef struct {
    u32 index;       // data-file index, must satisfy 2 <= index <=
                     // file_count-2 (0/1 are table carriers, the last is
                     // the degenerate end marker)
    const u8 *data;  // replacement bytes (may be NULL iff size == 0)
    u32 size;
} gt2_vol_replacement_t;

// Upper bound for one replacement (sanity, not a format limit).
#define GT2_VOL_PACK_MAX_FILE (0x40000000u)

// Build a repacked VOL extent blob from `image_path` (raw or cooked).
// With rep_count == 0 the blob is byte-identical to the source extent.
// Rejected with INVAL: bad index (0/1/last/out of range), duplicate index,
// data == NULL with size != 0, oversize entry. Caller frees *blob_out.
gt2_vol_status_t gt2_vol_pack(const char *image_path,
                              const gt2_vol_replacement_t *reps, u32 rep_count,
                              u8 **blob_out, u32 *blob_size_out);

typedef void (*gt2_vol_entry_visit_fn)(const gt2_vol_entry_t *entry,
                                       void *ctx);
// Visit a directory listing ('/' or '' = root). Includes '..' links and
// the END-flagged last entry.
gt2_vol_status_t gt2_vol_list_dir(const gt2_vol_t *vol, const char *path,
                                  gt2_vol_entry_visit_fn fn, void *ctx);
