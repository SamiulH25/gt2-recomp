#pragma once
// GT2 native port: CD image sector I/O.
//
// Reads both image flavors:
//   - cooked ISO (2048 B/sector; e.g. produced by tools/vol_dump.py cook)
//   - raw single-track dump (2352 B/sector MODE2, user data at +24 —
//     matches tools/extract.py bin_to_iso).
// All offsets are in "cooked space" (LBA*2048); the raw mapping is
// internal. Behavior reference: SCUS cd_read (0x8005D7D0) + low-level
// reader (0x8007AB78); the game's LBA base for VOL comes from RAM
// (see decomp/docs/gtfs_notes.md Q4/Q8).

#include "gt2/types.h"

typedef struct gt2_cd gt2_cd_t;

typedef enum {
    GT2_CD_OK = 0,
    GT2_CD_ERR_IO,
    GT2_CD_ERR_NO_MEM,
    GT2_CD_ERR_INVAL,
} gt2_cd_status_t;

gt2_cd_status_t gt2_cd_open(const char *path, gt2_cd_t **out);
void gt2_cd_close(gt2_cd_t *cd);
// Nonzero when the image is a raw 2352 dump.
int gt2_cd_is_raw(const gt2_cd_t *cd);

// One 2048-byte sector by LBA.
gt2_cd_status_t gt2_cd_read_sector(gt2_cd_t *cd, u32 lba, u8 out[GT2_SECTOR_SIZE]);
// Arbitrary byte range in cooked space (may span sectors).
gt2_cd_status_t gt2_cd_pread(gt2_cd_t *cd, u32 off, void *buf, u32 len);

const char *gt2_cd_strerror(gt2_cd_status_t st);
