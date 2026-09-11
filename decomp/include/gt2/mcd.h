#pragma once
// GT2 native port: PSX memory-card image parser (.mcd, 128 KB raw).
//
// Layout (PSXSPX memory-card data format; verified against a freshly
// formatted card from the game): 16 blocks x 64 frames x 128 B.
// Block 0: frame 0 header ("MC" + XOR checksum at 0x7F), frames 1..15
// directory ({u32 state; u32 size; u16 next; char name[21]; u8 0;
// checksum at 0x7F}), frames 16..35 broken-sector list, 36..55
// replacement data, 56..62 unused, 63 write test. File data lives in
// blocks 1..15 chained via dir next (block-1 based, FFFF = end).
// GT2 filenames: "BA"+"SCUS-94488"+8 chars (region + game code).

#include "gt2/types.h"

#define GT2_MCD_SIZE 131072u
#define GT2_MCD_FRAME 128u
#define GT2_MCD_DIR_ENTRIES 15u

typedef struct gt2_mcd gt2_mcd_t;

typedef enum {
    GT2_MCD_OK = 0,
    GT2_MCD_ERR_IO,
    GT2_MCD_ERR_NO_MEM,
    GT2_MCD_ERR_INVAL,      // bad image (size/magic)
    GT2_MCD_ERR_NOT_FOUND,
    GT2_MCD_ERR_NOSPACE,    // no free blocks/entries
    GT2_MCD_ERR_EXISTS,     // name already on card
} gt2_mcd_status_t;

// Directory states.
#define GT2_MCD_ST_FIRST 0x51u  // in use, first-or-only block
#define GT2_MCD_ST_MID 0x52u
#define GT2_MCD_ST_LAST 0x53u
#define GT2_MCD_ST_FREE 0xA0u  // freshly formatted
#define GT2_MCD_ST_DEL1 0xA1u  // deleted variants
#define GT2_MCD_ST_DEL2 0xA2u
#define GT2_MCD_ST_DEL3 0xA3u

typedef struct {
    u32 state;
    u32 size;           // bytes, multiple of 8 KB
    u16 next;           // next block-1, 0xFFFF = end
    char name[22];      // NUL-terminated (max 20 chars + NUL)
    int checksum_ok;
} gt2_mcd_dir_t;

gt2_mcd_status_t gt2_mcd_open(const char *path, gt2_mcd_t **out);
gt2_mcd_status_t gt2_mcd_open_mem(const u8 *data, u32 len, gt2_mcd_t **out);
void gt2_mcd_close(gt2_mcd_t *mcd);

int gt2_mcd_header_ok(const gt2_mcd_t *mcd);  // magic + checksum
// Number of in-use directory entries (state 0x51/0x52/0x53).
u32 gt2_mcd_file_count(const gt2_mcd_t *mcd);
gt2_mcd_status_t gt2_mcd_dir(const gt2_mcd_t *mcd, u32 idx,
                             gt2_mcd_dir_t *out);   // idx 0..14
// Whole file data for a first-block entry (follows the chain;
// malloc'd, caller frees).
gt2_mcd_status_t gt2_mcd_read(const gt2_mcd_t *mcd, u32 dir_idx,
                              u8 **data_out, u32 *size_out);

const char *gt2_mcd_strerror(gt2_mcd_status_t st);

// Format a fresh image in memory, byte-identical to a game-formatted
// card (MC/0x0E header, 15x free dir entries, empty broken list,
// erased data blocks).
gt2_mcd_status_t gt2_mcd_format(u8 out[GT2_MCD_SIZE]);

// Write a file (size must be a multiple of 8192, 1..15 blocks; name
// max 20 chars). Mutates the in-memory image; persist by writing the
// handle's bytes back out (see gt2_mcd_bytes).
gt2_mcd_status_t gt2_mcd_write(gt2_mcd_t *mcd, const char *name,
                               const u8 *data, u32 size);
// Delete a file by directory index (marks A1/A2/A3 + checksums).
gt2_mcd_status_t gt2_mcd_delete(gt2_mcd_t *mcd, u32 dir_idx);
// Raw bytes of the image (GT2_MCD_SIZE, valid while handle lives).
const u8 *gt2_mcd_bytes(const gt2_mcd_t *mcd);
