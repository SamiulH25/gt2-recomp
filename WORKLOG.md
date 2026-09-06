# Worklog — GT2 Hybrid Recompilation

Updated: 2026-09-06

## Done

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

- Perf: GT2 3D runs interpreted (5-7M interp insns/s); static overlay codegen for gt2_01 is the native-execution fix
- Texture seams at 4x bilinear (open, both 2D+3D)
- Prove end-to-end save (Sim race completion → card1.mcd GT2 blocks)

## Notes

- Uncommitted working tree: src/mods/gt2_widescreen.c, game.toml, docs/ENHANCEMENTS.md, docs/WIDESCREEN_RE.md, manifest.toml, psxrecomp (M runtime/src/main.cpp headless patch)
- GT2_01 is 3D render overlay; no screen-cull funnel found (GPU auto-clip assumed); world-space cull TBD if pop-in
- Anchor semantics UNVERIFIED (may be pointer not SXY in 2/4 funcs)
