# Simple Reader — engine pins

## KOReader release

- **KOReader**: v2026.07.1
- **koreader-base**: `6e4bc81af4e04f78d81677a323a780a71b29702a`
- **crengine**: `b32a88ffbbf505cf2ea5208c1a49002d70d3609f` (`third_party/crengine`)
- **MuPDF**: 1.27.2 (`third_party/mupdf`, Artifex source archive)

## SDK

- Target device: PocketBook Era (PB700), Allwinner B288
- Preferred: PocketBook SDK-B288-6.8
- **Actual build image used**: `larento/pocketbook-sdk:5.19-a13` with Clang 7 (`-std=gnu++17`) for crengine C++17. Same ARM EABI as firmware 6.x on B288. SDK-B288-6.8 remains the preferred upgrade when available as a Docker image.

## Cache constants

- Disk cache: `/mnt/ext1/system/state/simple_reader/cr3cache`, 64 MB
- In-memory storage factor: 16 (named in adapter init path)
- Log: `/mnt/ext1/system/state/simple_reader.log`, 64 KB cap

## Build

From repo root (Docker Desktop running):

```bat
scripts\build.bat
```

Reader is built via `scripts/build_reader.sh` inside the SDK container.

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

Required before `install.bat` edits `extensions.cfg`:

- [ ] Open EPUB, FB2, TXT, PDF
- [ ] Locked book and missing file show the message screen
- [ ] Sleep/wake keeps EPUB sentence and PDF page
- [ ] Night, light, brightness, warmth
- [ ] Font-size change keeps sentence (EPUB)
- [ ] Contents jump and go-to-page
- [ ] Full refresh on Nth page
- [ ] Clock and light taps do not flash the page
- [ ] After install, library opens those types with Reader; `extensions.cfg.simple-reader-bak` exists

## License

Reader.app is AGPL-3.0 (crengine GPL-2 + MuPDF AGPL). Other apps in this repo are unchanged.
