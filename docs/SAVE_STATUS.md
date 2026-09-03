# Save-data status (assumed working, not yet proven end-to-end)

## Verified

- **Card emulation is write-through**: `psxrecomp/runtime/src/memcard.c`
  `memcard_write_sector()` sets dirty and flushes the full 128K image to
  `card1.mcd`/`card2.mcd` immediately (fopen/fwrite/fflush/fclose per write).
  Crash-safe by construction; no exit-time flush dependency.
- **Cards format correctly**: fresh `saves/` gets `MC`-magic 128K images on
  first boot (via OpenBIOS LLE BIOS services, same path retail games use).
- **In-game card access works**: GT2 boots past its memory-card check with
  empty, freshly formatted cards and reaches the garage (no card errors).

## Assumed (per project decision 2026-09-02)

No GT2 save file has been observed on a card yet. Every menu session so far
ended in the garage/demo without triggering GT2's own save (post-race
autosave, replay save, or explicit save). The save _trigger_ was never
reached — this is a menu-navigation gap in testing, not a known emulation
failure. We proceed on the assumption that the first real save attempt will
persist via the write-through path above.

## Next step to prove it

Complete a Sim-mode race (or save a Replay Theater replay) and check
`saves/card1.mcd` for GT2 product blocks, then reboot and confirm the data
loads. The headless auto-input sweep in
`tools/patches/psxrecomp-gt2-headless.patch` drives toward arcade race
start; Sim-mode race completion is still open work.
