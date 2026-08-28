# xmp-furnace 1.0.0

**Native 32-bit XMPlay input plugin for Furnace, DefleMask, and FamiTracker.**

Copy `xmp-furnace.dll` next to `xmplay.exe`. Classic XMPlay is 32-bit; this
DLL is PE32 i386, fully static, **no companion DLLs**.

| Format | Magic / family | Played? |
|---|---|---|
| Furnace `.fur` | `-Furnace module-` / `Furnace-B` | **yes** |
| DefleMask `.dmf` | `.DelekDefleMask.` (raw or zlib) | **yes** |
| FamiTracker `.ftm` / `.0cc` / `.dnm` / `.eft` | `FamiTracker Module` / `Dn-FamiTracker Module` | **yes** |
| X-Tracker DDMF | `DDMF` at offset 0 | **no** — use [OpenMPT](https://support.xmplay.com/files_view.php?file_id=660) (`xmp-openmpt`) |
| XMPlay built-ins | `mod` / `xm` / `it` / `s3m` / `mtm` / `mo3` / `umx` | **no** |
| TFM / Future Composer | `.tfe` / `.fc` | **no** (ZXTUNE already has TFE) |

Display name: **Furnace / DefleMask**.
Registered extensions (one string, no `MULTIEXT`):
`fur/dmf/ftm/0cc/dnm/eft`.

This is **not** a Winamp `in_*` wrap. Native `XMPIN` only.

The engine is **Furnace** by [tildearrow](https://github.com/tildearrow/furnace)
(headless `DivEngine`). There is **no libopenmpt** in this plugin. XMPlay will
pick OpenMPT for X-Tracker DDMF and this plugin for DefleMask / Furnace /
FamiTracker.

## Install (32-bit XMPlay)

1. Quit XMPlay.
2. Copy **`xmp-furnace.dll`** into the XMPlay program folder (same directory as
   `xmplay.exe`). Native input plugins must use the `xmp-` prefix.
3. Start XMPlay. Options → Plugins / Input should list
   **Furnace / DefleMask 1.0.0**.

If you already have `xmp-openmpt` installed, leave it — it plays X-Tracker
DDMF. This plugin does not claim those files.

## Crash-safe load path

- `DllMain` only calls `DisableThreadLibraryCalls`. No decoder init, no UI,
  no `DivEngine` construction.
- `XMPIN_GetInterface` checks `face == XMPIN_FACE` (4), grabs the IN / MISC /
  FILE tables, and returns a **static** `XMPIN` struct. Wrong face → `NULL`.
- `CheckFile` magic-probes Furnace / DefleMask / FamiTracker only. DDMF and
  classic modules are rejected. Furnace is **not** started here.
- `Open` slurps the file (cap 128 MiB; zlib-wrapped modules 8 MiB compressed),
  then constructs `DivEngine`. `Close` destroys that engine.
- `SetLength(..., TRUE)` — tracks are seekable.
- All string writes are bounded. Pointers are null-checked.

## Seeking

Position 0 always restarts. Other positions use Furnace
`calcSongTimestamps` plus `setOrder` / `playToRow`. If timestamps cannot
be computed, only restart works and a mid-song seek returns failure.

## Build

On Debian/Ubuntu:

```bash
sudo apt-get install mingw-w64 g++-mingw-w64-i686 g++ cmake make curl \
    zlib1g-dev libz-mingw-w64-dev zip
# POSIX thread model (std::mutex):
sudo update-alternatives --set i686-w64-mingw32-g++ /usr/bin/i686-w64-mingw32-g++-posix
sudo update-alternatives --set i686-w64-mingw32-gcc /usr/bin/i686-w64-mingw32-gcc-posix
```

Furnace sources live under `third_party/furnace` (clone with submodules).
Then:

```bash
cd xmp-furnace
/usr/bin/make          # 32-bit dist/xmp-furnace.dll + host tests
/usr/bin/make dll      # dist/xmp-furnace.dll only
/usr/bin/make test     # magic-byte probe
/usr/bin/make test-render  # Furnace host render of local samples
/usr/bin/make pack     # /workspace/xmp-furnace-1.0.zip
```

This builds a **full** (nearly full) headless Furnace engine so every chip
system in a `.fur` song can play. `XMP_DMF_STRIP` / the DefleMask-only chip
cut are **not** used. libopenmpt is not downloaded or linked.

The first build configures and statically compiles the engine. That step
takes several minutes.

Outputs land under `dist/`. The DLL is fully static: **no companion DLLs**.

## Tests

`make test` runs the magic-byte probe:

- accept `-Furnace module-` / `Furnace-B`
- accept raw / zlib DefleMask `.DelekDefleMask.`
- accept FamiTracker / Dn-FamiTracker
- **reject** X-Tracker `DDMF`
- reject IMPM (IT), TFM, FC, garbage / fake zlib

`make test-render` inits Furnace, loads local samples under
`tests/samples/`, renders about two seconds of float stereo, and asserts
the RMS is not silence. It also refuses a synthetic `DDMF` header.

## Architecture

```
src/dmf_probe.c|.h       portable magic + zlib probe (claim vs reject)
src/furnace_player.cpp   DivEngine wrapper (open/process/seek/close)
src/xmp-furnace.cpp      XMPIN plugin (Furnace only)
src/xmp-furnace.def      export XMPIN_GetInterface
include/xmplay/          official XMP-SDK headers (un4seen)
tests/test_probe.c       host-side magic + CheckFile
tests/test_furnace_render.cpp  host-side render + DDMF reject
third_party/furnace      chiptune decode (full chip set; headless)
```

## License

The **combined plugin** is **GPLv2** because it statically links Furnace
(GPL-2.0-or-later). See `LICENSE`.

- Plugin sources: GPLv2
- Furnace: GPL-2.0-or-later (`third_party/furnace`) — tildearrow and contributors
- XMPlay SDK headers: copyright un4seen developments

## Limitations

- Does not wrap a Winamp `in_*` plugin.
- Does not play X-Tracker DDMF (use OpenMPT).
- Does not claim `.tfe` / `.fc`.
- Files larger than 128 MiB, or zlib-wrapped modules larger than
  8 MiB compressed / 32 MiB uncompressed, are refused.
- Furnace seeking is order/row based (see above). Restart always works.
- Furnace is inited at 48 kHz dummy audio. `SetFormat` reports that rate
  rather than rebuilding the engine.
- No config dialog, no pattern vis.
- Built for XMPlay 3.8+ (`XMPIN_FACE` 4). Older faces return `NULL`.
