# GT2 Hybrid Recompilation

Gran Turismo 2 (USA) Simulation Mode v1.2 — PSX → native PC (hybrid static
recomp + incremental decomp). Boots to the garage menu, renders races.
Memory cards format and pass GT2's card check; end-to-end saving is not yet
proven (`docs/SAVE_STATUS.md`).

Status: menu stable (26k+ frames, no crash), garage renders with cursor
navigation, enhancement batch 1 shipped (GL resolution/audio config +
widescreen mod scaffold, default off). See `docs/ENHANCEMENTS.md`,
`docs/WIDESCREEN_RE.md`, `docs/BOOT_ATTEMPT.md`.

## Prerequisites

- Your own legal dump: the `.bin` + `.cue` of
  `Gran Turismo 2 (USA) (Simulation Mode) (v1.2)` (660M bin, MD5
  `e697a485b661a12fa6c327186c336a31` — see `[prepare_disc]` in
  `game.toml`). Place both in `disc/` (gitignored, never committed), then
  extract the system files (`SCUS_944.88`, also gitignored):
  `isoinfo -R -i <your_2048_iso> -x '/SCUS_944.88;1' > disc/SCUS_944.88`
  (or the full pipeline `python3 tools/extract.py` — see its header for the
  2352→2048 conversion step).
- Tools: `cmake`, a C/C++ compiler, `zlib` dev files, `python3`
  (+ `capstone` for `tools/disasm.py`), `isoinfo` for extraction tools.
  SDL is fetched automatically (pinned SDL3).
- `generated/` (recompiled SCUS, 358 shards) is gitignored but required to
  build. Either reuse an existing tree or regenerate (below).

## Build

```bash
git submodule update --init                       # psxrecomp framework
cd psxrecomp && git apply ../tools/patches/psxrecomp-gt2-headless.patch && cd ..
# ^ SIO-level auto-input + VRAM dump; already applied in this checkout
# (see docs/HEADLESS_INPUT_PATCH.md). Skip if psxrecomp already shows it
# as a local modification (git -C psxrecomp status).

# Optional: regenerate recompiled code (needs game.toml + disc/)
cmake -S psxrecomp/recompiler -B /tmp/psxrecomp_build -DPSXRECOMP_ENABLE_CHD=OFF
cmake --build /tmp/psxrecomp_build
/tmp/psxrecomp_build/psxrecomp-game --config game.toml   # -> generated/

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
# -> build/Gran_Turismo_2_Recompiled (+ game.toml + disc/ staged beside it)
```

Notes:

- `PSX_RECOMP_UI` is forced OFF in `CMakeLists.txt`; no extra flags needed.
- The post-build step copies `game.toml` and `disc/` images next to the
  binary. If you edit the repo `game.toml` afterwards, rebuild (or copy it
  over `build/game.toml`) or your change won't take effect.
- GT2 overlays (`GT2.OVL`, 6 members) are handled at runtime via the
  interpreter + GTFS reader — no overlay codegen step needed for a working
  build (`docs/OVERLAYS.md`).

## Run

The binary writes mod state next to itself (`<exe_dir>/mods/state.toml`),
so run it from a **user-writable directory** — copy/symlink the binary,
`game.toml`, and `disc/` out of `build/` if that tree is root-owned:

```bash
mkdir -p /tmp/gt2-run/{disc,mods,saves,bios}
cp build/Gran_Turismo_2_Recompiled game.toml /tmp/gt2-run/
ln -s "$PWD/disc/"*.bin "$PWD/disc/"*.cue disc/SCUS_944.88 /tmp/gt2-run/disc/
cp psxrecomp/bios/openbios.bin /tmp/gt2-run/bios/
cd /tmp/gt2-run
```

Windowed (needs a display):

```bash
./Gran_Turismo_2_Recompiled --game game.toml --renderer opengl
```

Headless (no display; auto-input drives the menu flow, dumps frames):

```bash
SDL_AUDIODRIVER=dummy PSX_LOW_LATENCY_INPUT=0 GT2_AUTO_INPUT=1 GT2_AUTO_DUMP=1 \
  ./Gran_Turismo_2_Recompiled --game game.toml --headless --renderer software
# gt2_dump_<frame>.png lands in the cwd (from frame 6000 on)
```

Windowed without a display (Xvfb; X11 keyboard won't reach the game, so
keep the SIO auto-input):

```bash
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 PSX_LOW_LATENCY_INPUT=0 \
GT2_AUTO_INPUT=1 PSX_FPS_TELEMETRY=1 \
  xvfb-run -a ./Gran_Turismo_2_Recompiled --game game.toml --renderer opengl
```

Environment knobs:

| Var | Effect |
|---|---|
| `GT2_AUTO_INPUT=1` | SIO-level START/CROSS/d-pad phases: license → garage → arcade/car/track → drive (`docs/HEADLESS_INPUT_PATCH.md`) |
| `GT2_AUTO_DUMP=1` | Headless VRAM dumps `gt2_dump_<frame>.png` |
| `PSX_LOW_LATENCY_INPUT=0` | Required for windowed tests (else re-sample stomps slot 0) |
| `SDL_AUDIODRIVER=dummy` | Silent runs (no host audio) |
| `PSX_FPS_TELEMETRY=1` | `[FPS]` lines on stderr + window title |

## Configure

- `game.toml` `[video]` / `[audio]`: renderer, supersampling, AA,
  bilinear, geometry correction, SPU HQ — what each setting does and what
  is verified is in `docs/ENHANCEMENTS.md`.
- Widescreen is a mod (`mods/gt2-widescreen/`, default off, world correct /
  HUD stretched); sprite-tag + cull RE status is in
  `docs/WIDESCREEN_RE.md`.
- Headless input phases, screenshots, save status: `docs/`,
  `docs/screenshots/`.

## Repo map

- `game.toml` — game + runtime config (source of truth; staged to build)
- `src/mods/` — game-owned enhancement plugins (linked into the binary)
- `mods/` — mod packages (manifests)
- `generated/` — recompiled SCUS output (regenerate, don't hand-edit)
- `seeds/ghidra_funcs.txt` — 1179 codegen seed addresses
- `tools/` — `extract.py` (2352→2048 + files), `gtfs_extract.py` (asset
  FS), `split_ovl.py` (overlay members), `disasm.py` (capstone sanity),
  `ghidra/LoadGT2Overlays.py`, `patches/`
- `docs/` — architecture, boot milestones, GTFS, overlays, enhancements,
  widescreen RE, headless input, saves
