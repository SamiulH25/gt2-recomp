# Visual proof: GT2 garage menu renders + cursor navigates

Recompiled `Gran_Turismo_2_Recompiled` (358 shards, OpenBIOS LLE) run headless
with `GT2_AUTO_DUMP=1`, software renderer, auto-input patch:

- `docs/screenshots/garage-menu.png` (frame 6000): HOME/GARAGE tabs, icon row,
  big GARAGE header, N/A slot, cursor mid-screen, bottom bar `1 days`, `10,000`
  credits with car icon. This is GT2 Simulation Mode's main garage screen.
- `docs/screenshots/garage-cursor-trophy.png` (frame 6300): yellow cursor over
  the trophy (rightmost) icon after UP/LEFT navigation input.
- `docs/screenshots/garage-cursor-up.png` (frame 6900): cursor over the
  up-arrow icon. Proves d-pad input reaches the game and the menu responds.

Sequence: 33 dumps from frame 6000 to 24300+, all 512x480. Frames 6300-7200
show the cursor on different icons (md5s differ); all other frames are
byte-identical to the base garage (stable idle, no crash over 24k frames).

Not yet: CROSS selection did not enter the arcade/car-select screens — the
cursor was mid-row when CROSS was pressed. Next input iteration should hold
direction until the cursor rests on the checkered-flag icon, then press CROSS.
