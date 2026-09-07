#pragma once
// GT2 native port: fixed-width integer aliases + shared constants.
// Matches the original game's LP32 data model (all target types are
// 32-bit int / 32-bit pointers), so layouts stay exact on host.

#include <stddef.h>
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

// PSX CD-ROM / GTFS geometry (verified against the US 1.2 sim disc).
#define GT2_SECTOR_SIZE   2048u
#define GT2_VOL_LBA       473u                      // GT2.VOL starts here
#define GT2_VOL_BASE      ((u32)GT2_VOL_LBA * GT2_SECTOR_SIZE)
