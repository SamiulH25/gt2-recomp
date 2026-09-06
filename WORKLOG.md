# Worklog — GT2 Hybrid Recompilation

Updated: 2026-09-06

## Done

- [2026-09-06] Menu-navigation campaign (auto-input v1-v6, screenshot-driven): title screen flawless; garage + GAME STATUS + My Home hub all render correctly. Decoded the cursor (magnetic arrow; hotspots hold, empty space snaps to center). Reached My Home hub once via EXIT; hub->arcade still open (exit->hub transition flaky ~1/5, transition-eaten taps). v6 uses holds + armored windows. Attract-demo mode added (GT2_AUTO_DEMO=1, untested). Blocker status: no run has reached a real race yet — all prior "race" windows were car-select/status screens; threading verdict is provisional pending in-race data
- [2026-09-06] Profile-first v2: static-dispatch ledger proves static backend serves ~500K overlay calls by frame 8.7K (menu phase) with loader shards at zero — but parent/funcB get NONE of them. Hit histogram (`[gt2_phot]`, `tools/patch_overlay_prof.py` hit sites) names the real menu-hot set: 0x800163C8 (72B, ~1/frame) + 0x80016410/0x800167A4/0x800167FC/0x80016980/0x800168AC, all in overlay member 4FC1D21E — parent/funcB's member 74BDB962 is simply not resident outside its scene. P2's "parent cold" finding confirmed structurally. Also: PSX_DEBUG_TOOLS=OFF in this build, so no TCP 4370 (phase_hot unreachable); env-log profiling is the path. Pending: in-race histogram (run launched) to name the race-phase hot set before picking funcB-inline vs render-thread
- [2026-09-06] P2 batch-dispatch: PARKED parent-hijack after 10+ build-run cycles. Findings: (1) arena fix real (host ptrs can't flow through guest regs — was the FEE90033 crash); (2) CPS-suspend incompatibility: parent/funcA are CPS state machines, interpreter route expects callee-suspend (CRES_NL_PC), return-1 kills boot deterministically at 9.3M insns — exonerated cycles/IRQ/scratch/GTE/RAM/mailbox one by one, noapply run proves pure control-flow kill; (3) cold-path discovery: parent emits NOTHING in-race (20K calls total=0), 4 funnel entries never fire (census: 1250 untagged polys/frame) — true hot path is funcB's inline RTPS loop. Infra kept (pool/clone/scratch-priv/detach, all env-gated + inert). Next perf step: profile-first on funcB-inline or host-side render thread
- [2026-09-06] Widescreen right-margin cutoff: GL scissor widened left-only, never right — fixed symmetric (psxrecomp f453519a), built + smoke-tested, pushed; awaiting in-race playtest confirm. Game-side outcode cull (camsetup per-object bounds) remains suspect if cutoff persists
- [2026-09-05] Fixed 18 root-owned files (src, build artifacts, scratch logs) → bob2142:bob2142
- [2026-09-05] All 6 overlays static (1305 funcs): boot interp spike gone, steady 45K insn/s = kernel/scheduler territory. Game code fully native; pushed
- [2026-09-04] Static overlay codegen unblocked: synth loop was O(n²) string rebuilds (25MB src × 10k iters); shift-tracking fix in psxrecomp/tools/compile_overlays.py, full gt2_01 static compile running
- [2026-09-03 live] Squash-mode 16:9 (native_wide=false, gte_game_mode=true): killed side-margin flicker; native-wide double-draw at 4x was the slowdown/barely-render. Texture seams at 4x still open
- [2026-09-03] Widescreen sprite-tag plugin (overlay-aware function_entry_plugin at 4 gt2_01 PCs + prologue guard, hud_sprt_squash, 16:9 active) — runtime verification pending
- [2026-09-03] Docs: fps pacer 60Hz measured, widescreen table fix, manifest 1.0.0
- [2026-09-03] README build/run docs + WIDESCREEN_RE static findings
- [2026-09-02] Enhancements batch 1: resolution/audio config + widescreen mod scaffold
- [2026-09-02] Input breakthrough (SIO auto-input), garage visual proof, save status

## Next

- Perf profile-first (VERDICT, PROVISIONAL — menu-phase data only): automation never reached a real race (all "race" windows were car-select/status screens), so the no-hotspot finding covers menus only. True in-race residency (74BDB962 scene member?) still unmeasured. Navigation to a real race is the critical path; re-run profile AFTER first real race. (Original verdict text kept below for the record: SIGPROF+ledger+BENCH converge on menus — 60fps held, wall ~ guest 25-30 / render 14-36 / pacer rest, no coarse guest hotspot, biggest item SCUS func_80094DC8 59KB/~10% wall. Infra kept env-gated + inert.)
- Perf: GT2 3D runs interpreted (5-7M interp insns/s); static overlay codegen for gt2_01 is the native-execution fix
- Texture seams at 4x bilinear (open, both 2D+3D)
- Prove end-to-end save (Sim race completion → card1.mcd GT2 blocks)

## Notes

- Uncommitted working tree: src/mods/gt2_widescreen.c, game.toml, docs/ENHANCEMENTS.md, docs/WIDESCREEN_RE.md, manifest.toml, psxrecomp (M runtime/src/main.cpp headless patch)
- GT2_01 is 3D render overlay; no screen-cull funnel found (GPU auto-clip assumed); world-space cull TBD if pop-in
- Anchor semantics UNVERIFIED (may be pointer not SXY in 2/4 funcs)
