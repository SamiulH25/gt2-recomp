# Menu/UI flow notes (Phase F native-port research log)

Game addresses are VRAM (`overlays/gt2_02.exe` at `0x80010000`, SCUS
thunks as noted). Port: `gt2/menu.h` (`record_init`, `counter_bump`,
`emit_tick`); driver `tools/menu_tick.py`; test `decomp/tests/test_menu.c`
(15-tick-vector xcheck: sequences + emitter flags + packet bytes +
short-path cells, all byte-exact vs emulation).

## Menu phase spec (auto-input v6 — the executable spec)

`tools/patches/psxrecomp-gt2-autonav-v2-v6.patch` + `docs/HEADLESS_INPUT_PATCH.md`.
Active-low pad bits; phases log as `[gt2_auto] f=<n> phase=<name>`:

| frames | phase | action |
|---|---|---|
| <120 | pre | nothing |
| 120–2400 | license | START taps — license screens |
| 2400–4800 | title | CROSS taps — title → garage |
| 4800–6000 | settle | garage screen |
| 6000–6400 | exit-hold | hold LEFT — parks arrow on EXIT |
| 6400–7200 | exit-settle | settle |
| 7200–10200 | hub-enter | CROSS ~20 clicks — EXIT → My Home hub |
| 10200–11000 | hub-settle | settle (arrow assumed on big GARAGE) |
| 11000–11400 | hub-up | hold UP — hub top icon row (assumed grid) |
| 11400–12000 | hubup-settle | settle |
| 12000–12240 | hub-right | RIGHT ×2 — grid → checkered (assumed race entry) |
| 12240–12800 | aim2-settle | settle |
| 12800–15200 | sel-area | CROSS — assumed area entry |
| 15200–16000 | area-settle | settle |
| 16000–18400 | sel-car | CROSS — assumed car pick |
| 18400–19200 | car-settle | settle |
| 19200–21600 | sel-track | CROSS — assumed track pick |
| 21600–22400 | track-settle | settle |
| 22400–24800 | sel-go | CROSS — assumed confirm/go |
| 24800–60000 | drive | CROSS held (gas) + UP nudges |

Cursor model (screenshot-proven): magnetic arrow; hotspots hold it
indefinitely, empty space snaps back to center rest.

## Nav verdict (2026-09-09 live run — still blocked)

Headless autonav v6 + `GT2_DEEPLOG=1` + `GT2_AUTO_DUMP=1`, 207900 guest
frames: the game NEVER left the garage (dumps at f=10800/hub-settle,
f=43500/drive, f=207900/final all show GARAGE; cursor center-rest or on
trophy). Evidence: `docs/screenshots/garage-at-drive-phase.png`. The
Sept-6 1-in-4 EXIT→hub transition reproduced 0 times this run. A.1-nav
and A.3-save stay blocked; menu decomp proceeds statically (this module)
plus recorder-led live observation.

## F.1a port: menu record + tick (gt2_02)

Found via the Sept-6 menu-hot set: 6 PCs, one (`0x800163C8`, ~1/frame)
in a 144 B init region, five in a 0x59C tick at `0x80016410`. Zero static
JAL callers anywhere in the member — all dispatch-reached (indirect),
consistent with overlay self-registration (Phase D.2/E.3).

### Record layout (0x14 B)

| off | field | init | tick use |
|---|---|---|---|
| +0x00 | u8 flags (bit6 set; bits 0–2 select) | tpl+0 | stanza select, ±t2 adjust mask 0x60 |
| +0x01 | u8 spare | — (kept) | — |
| +0x02 | s16 blend source | tpl+2 | × (12−cnt) ease term |
| +0x04 | s16 ease base | — (kept) | s4 ± t2 adjust, fill base |
| +0x06 | s16 ease base | — (kept) | s6, fill high half |
| +0x08 | u8 blend base | tpl+1 | × cnt ease term |
| +0x09..0x0B | holes | — (kept) | — |
| +0x0C | u32 list guest addr | — (kept) | idle-compare cookie + list block |
| +0x10 | s16 counter | −1 (idle) | see bump; idle-vs-list check |
| +0x12 | u16 scale | 0x80 | × blend output (lh sign-extends) |

List block (10 B): u32+0 / u32+4 (copied into every packet fill),
u16+4/+6 (ease math, zero-extended then halved), u16+8 (emit_B flags).

### counter_bump (0x800163C8) — verified table

−1 sticks; below −1 climbs (+1 toward −1); 0..11 increment; ≥12 clamps
to 12; 32767 wraps to −32768 (kept, < 12). Emu vectors in the test.

### emit_tick (0x80016410) — control flow

- Idle: `(s32)cnt == (s32)list_addr` → return (effectively never taken;
  ported exactly).
- Head: `t2 = (s16)list.h4 >> 1`; `s4 ±= t2` iff `(b0&0x60)` is
  0x20/0x40.
- cnt == −1 → return. cnt < −1 → short path: fixed-point ease
  (`k = cnt+13`, div-3 magic), two `emit_S` cell calls
  (`0x8006B61C`, hooked), tail `emit_B(fp, 0x20)`.
- cnt ≥ 0 → blend (skipped at ≥ 12, leaving s2 = raw b8, s5 = 1):
  `s2 = div3(b8·cnt)`, `s5 = div3(h2·(12−cnt))`, `a3 = b8 − s2`
  (the 0x20 in the short-path delay slot is NOT live here — a1 still
  holds cnt; caught by the cnt=0 → flags-0 vector, not by reading).
  Then `t4lo = s2·x12`, `s2 >>= 7`, `s7 = (b0&0x18)<<2`.
- First pair iff `cnt < 12 && (b0&4)`: two `emit_A(compose(a3)|0x2000000)`
  with `(s4−s5)` / `(s4+s5)` fills, then `emit_B(h8|s7)`.
- Bit1 clear → shortcut: one `emit_A(compose(s2))` (NO 0x2000000 top
  bit), `(s4−shr1)` fill. Bit1 set → full stanza: `emit_A`,
  `(s4−shr1)` fill, `emit_B(h8|s7)`.
- Bit0 set → t4gate (`[sp+0x60]`, game a2): nonzero runs the
  `emit_A(0x02000000)` + `(s4−shr1)` + `emit_B(h8)` stanza; zero
  re-derives `s2 = t4lo>>8` (the `[sp+0x28]` reload is always-fresh —
  delay-slot store — never stale; a stale-slot theory cost one debug
  round-trip and died to a slot watch).
  Then `emit_A(compose(s2)|0x2000000)` + fill + `emit_B(h8|0x40)`
  (delay-slot OR), then the tail joins: `emit_B(h8|s7)`.
- Tail (all blend paths): `emit_B(h8 | s7_slot)`; returns last packet.

Call-shape proof (emu, rec = b0/h2/h4/h6/b8/list/cnt/x12 fixed):
`-1:[] 0:{AB} 3:{AB} 3+bit0:{AB} 3+bits01:{ABABB}
3+bits012:{AABABABB} 12:{AB} −5:{SSB} 12+bits02:{ABABB}
3+bits012+t4:{AABABABABB} 0+bits02:{AABAB} 11+bit2:{AABAB}`.

### Emitter protocol (SCUS 0x8007DA44 / 0x80081478)

GPU packet-arena appends at `[0x801C93EC]` (the mail cell from the
render-loop plan): unaligned `lwl/swl` header store, tag byte 1 (B) vs
4 (A), flag mask `|0xE100` (B) vs `^0x6400` (A), arena bump, return new
top; the caller fills 12 B (computed word + list w0/w4) at the returned
pointer. So each stanza = 8 B header + 12 B fill = 20 B packet. B
returns are never filled by the tick (tick returns the arena top).
Full arena port stays F.3 render-loop work; the protocol above is its
ground floor.

### Not ported (documented gaps)

- Wrapper `0x8001636C`: calls inflate-index pair `0x80016254` /
  `0x8001DA08` (big frames 0x7250/0x3D60, `jal gzSetup 0x80082FAC`
  on SCUS data `0x80021104`/`0x80022D80`, index by task-mem byte
  `[0x801C98E0]` with shift-multiply stride + alignment check).
  Needs real gzip bytes + pages; same class as b2's async DMA.
- Short-path callee `0x8006B61C` (seeded SCUS): color-splat packet
  prologue via `0x8007E0B0`; hooked in the driver, args logged
  (fp + sp+0x10 cell).

## Live dispatch data (same run — F.1b fuel)

- `[gt2_miss]` top-6: `0x800177CC/A4/F0/BC/D4` + `0x80017918`, EXACTLY
  20058 hits each, frozen from early in the run (line 67) to the end:
  a boot/menu-entry-era per-frame 6-call lockstep set served by the
  interpreter (compiled somewhere, occupant not resident), stopping
  after ~20K frames. No member has prologues AT these PCs (mid-function
  targets — CPS-continuation class). Attribution needs resident-CRC
  logging (recorder upgrade, 3 lines: log CRC/member with each miss).
- `[gt2_amiss]` empty the whole run: every called address is compiled
  somewhere — misses are residency (band/CRC), never unknown code.
- `[gt2_phot]` steady-state garage set: `0x8001F2C8` (3.4M),
  `0x800215C8`/`0x8001BA20` (1.8M), `0x8001BEB4`/`0x8001F604`/
  `0x8001F594` (~0.6M) — a different set than the Sept-6 menu set
  (`0x800163C8`… in gt2_02): garage-phase residency differs from
  boot-menu residency. Enclosing-function naming is F.1b.
- Totals at end: static_checks 38M / hits 25M / vmiss 12.8M /
  amiss 46M — static dispatch serves ~2/3 of overlay calls; the
  interpreter carries the CPS-continuation third.

## Open (F.1b → F.2)

- Name the 6 lockstep miss callees + 6 phot hitters. LEAD (2026-09-09):
  all 6 miss PCs sit inside ONE 480 B function, gt2_01 `0x80017784` —
  a mode-indexed menu dispatcher: guards on `+0x5D0(s0)` and the
  task-mem mode byte `[0x801C98E0+0xBF7C+0xA]`, then a 10-way computed
  goto (`jr` into `[0x8002F0B8+(mode-1)*4]`) plus per-state arms on the
  `+0x408` flag byte (values 2/3/4/7/11 → helpers `0x80017174/0x80017200/
  0x800171B8/0x8001710C`, returns 4/7/9/11). Its tail couples to the b3
  MARK tables (`+0x3C74` index math from task-mem halves +0x582/+0x584,
  halfword copy to +0x58 when both negative; sets +0x5D1). CRITICAL:
  the 10 table words are high-entropy non-pointer bytes in the SCUS file
  — the table is RUNTIME-FILLED (second self-registration target
  alongside `0x801EF610`; same writer hunt as Phase E residue).
- Member residency timeline: boot-menu era = gt2_02 resident (Sept-6
  tick set) with cross-calls into gt2_01 (the vmiss lockstep-6);
  garage era = gt2_01 resident (phot = funnel/batch-loop region
  `0x8002106C..0x800234F8` + pre-funnel `0x8001BA20/0x8001BEB4`).
  Still to prove with resident-CRC logging (recorder upgrade).
- `0x8006B61C` + wrapper inflate pair ports (needs pages/gzip).
- Save system still blocked on nav (A.3); PGXP A/B, 4x seams carried.
