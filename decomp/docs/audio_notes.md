# Audio notes (Phase C.5 probe, 2026-09-08)

## SEQ (`spu_10.seq`, 5486 B)

Raw event stream, no magic: head
`7f022cb44f0278af760200b44f023cbe580210b4290204be630214c0790214a3…`.
Presumably the standard PSX `.SEQ` (SpuSt* sequence) layout, but no
header/structure has been verified against the SPU upload path (voice
allocation past init — trailing `0x8007916c`, global `0x80092E88` — is
itself still open). Playback/decode deferred to Phase D (needs the
overlay-side sequence player + SPU backend beyond `gt2_spu` init).

## Open

- SEQ header/event grammar (compare against known PSX SEQ docs + the
  in-game player).
- `INST` sample blob (tbl[11564] candidate was rejected for the weight
  table; re-examine as audio here), `sound/` dir tail slots.
- XA streaming (leave at authentic timing; see fast-loading decision).
