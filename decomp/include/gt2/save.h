#pragma once
// GT2 native port: save-data utilities (memcard blocks come later).
//
// Behavior reference: SCUS gt2_save_crc32 (0x80083178) — standard
// CRC-32 (poly 0xEDB88320, init/xorout 0xFFFFFFFF) over the table at
// 0x800A6ACC. Confirmed by emulation ("123456789" -> 0xCBF43926).

#include "gt2/types.h"

// Standard CRC-32 of `len` bytes at `data`.
u32 gt2_crc32(const u8 *data, u32 len);
