# NaSkilla Reader — engine pins

## KOReader release

- **KOReader**: v2026.07.1
- **koreader-base**: `6e4bc81af4e04f78d81677a323a780a71b29702a`
- **crengine**: `b32a88ffbbf505cf2ea5208c1a49002d70d3609f` (`third_party/crengine`)
- **MuPDF**: 1.27.2 (`third_party/mupdf`, Artifex source archive)
- **FreeType (SDK)**: 2.7.1 (older than 2.10; `FT_Error_String` stubbed in `crengine_pb/ft_compat.c`)

## SDK

- Target device: PocketBook Era (PB700), Allwinner B288
- Preferred: PocketBook SDK-B288-6.8
- **Actual build image used**: `larento/pocketbook-sdk:5.19-a13` with Clang 7 (`-std=gnu++17`) for crengine C++17. Same ARM EABI as firmware 6.x on B288. SDK-B288-6.8 remains the preferred upgrade when available as a Docker image.

## Paths

| Item | Path |
|------|------|
| Settings | `/mnt/ext1/.naskilla.cfg` |
| Book meta cache | `/mnt/ext1/system/state/naskilla/bookmeta.cfg` |
| Debug flags | `/mnt/ext1/system/state/naskilla/debug.cfg` |
| Draw crash sentinel | `/mnt/ext1/system/state/naskilla/draw_inflight` |
| cr3cache dir | `/mnt/ext1/system/state/naskilla/cr3cache` |
| Log (64 KB cap) | `/mnt/ext1/system/state/naskilla.log` |
| Boot log (64 KB cap) | `/mnt/ext1/naskilla_boot.log` |
| App | `/mnt/ext1/applications/NaSkilla Reader.app` |

Document cache and HyphMan are currently disabled in the adapter.

## Render path (Stage 1)

- PDF: MuPDF gray pixmap → `page_buf` → canvas blit (unchanged).
- EPUB/FB2/TXT/…: crengine `LVDocView::Draw` into an 8 bpp buffer.
  - Production: scratch `LVGrayDrawBuf` + copy into `page_buf` (direct wrap of `page_buf` is unused; either path hit the same glyph-cache bug below).
  - Fallback: Inkview `page_text` when Draw fails or `render=text`.
  - Glyph cache items use `posix_memalign(16, …)` because `bmp` is `alignas(16)` and Clang emits NEON `vst1 … :128`; plain `malloc` is only 8-byte aligned on this ARM and caused `FATAL signal=7` (SIGBUS) in `LVFreeTypeFace::getGlyph`.
- Header chrome is forced off (`PROP_STATUS_LINE=1` and related SHOW_* flags) so Draw does not paint an author/chapter gauge under our own bar.
- Night colors come from crengine props; the UI does not invert the bitmap again.

### debug.cfg

Never rewritten by `config_save`. Edit over USB:

```
render=text|auto|pattern
draw_probe=0..6
crash_test=0|1
```

| draw_probe | Meaning |
|------------|---------|
| 0 | Diagnostics only (no Draw) |
| 1 | Header-off check |
| 2 | Draw into owned scratch, discard |
| 3 | Scratch Draw + copy into `page_buf` + blit |
| 4 | Same as 3 (styles via normal layout) |
| 5 | Cover/first page |
| 6 | Production: scratch Draw + copy (same as 3; direct wrap of `page_buf` crashes on Era FW 6.10) |

If `draw_inflight` is present at startup, the previous session died inside Draw: that launch forces `render=text` and deletes the sentinel.

Default shipped file: `config/naskilla_debug.cfg` → `debug.cfg` on first install only.

Unstripped binary kept on the PC as `build/NaSkilla.debug` for `addr2line`.

## Build

From repo root (Docker Desktop running):

```bat
scripts\build.bat
```

Reader is built via `scripts/build_reader.sh` + `scripts/link_reader.sh` inside the SDK container.

Host tests:

```bat
scripts\test_reader_host.bat
```

## Updating engines

1. Note the crengine submodule SHA and MuPDF version from the next **stable** KOReader release.
2. `git -C third_party/crengine fetch` and checkout that SHA.
3. Replace `third_party/mupdf` with that MuPDF source version.
4. Rebuild with `scripts\build.bat`.
5. Fix only `engine_crengine.cpp` / `engine_mupdf.cpp` if APIs moved.
6. Run host tests and the on-device checklist below.

## On-device release checklist

- [ ] Open text EPUB, cover EPUB, FB2, TXT, PDF — real page (not plain Inkview text for reflowable)
- [ ] Bold / italic / heading / image visible on EPUB
- [ ] Locked book and missing file show the message screen
- [ ] Sleep/wake keeps EPUB sentence and PDF page
- [ ] Night, light, brightness, warmth
- [ ] Font face/size, margins, spacing, publisher layout keep the sentence
- [ ] Contents jump and go-to-page
- [ ] Full refresh on Nth page
- [ ] Clock and light taps do not flash the page
- [ ] `render=text` in debug.cfg restores the old text path
- [ ] With sentinel present, next launch stays on text and does not crash-loop
- [ ] 200 page turns, RSS stable in the log
- [ ] After install, library opens those types with Reader; `extensions.cfg.simple-reader-bak` exists

## Baseline restore

Known-good pre-Stage-1 binary (device copy): `C:\PocketBook\baseline\NaSkilla Reader.app`.

## License

NaSkilla Reader.app is AGPL-3.0 (crengine GPL-2 + MuPDF AGPL). Other apps in this repo are unchanged.
