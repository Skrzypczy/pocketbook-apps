---
name: pocketbook-device-install
description: Connect, detect, build, install, and safely eject a PocketBook e-reader over USB for NaSkilla Reader and sibling apps. Use when installing .app files, copying to D:/applications, ejecting/rozłączanie USB, reading device logs, or editing naskilla debug.cfg over USB Drive mode.
---

# PocketBook device install

## Connect

1. Plug PocketBook via USB.
2. On device choose **USB Drive** (PC mass storage). While mounted, apps on the device do not run normally — eject before the user can test UI.
3. Detect drive: first letter `D`–`Z` where `X:\system\config` exists.

```powershell
Get-PSDrive -PSProvider FileSystem | Where-Object { Test-Path "$($_.Name):\system\config" }
```

Windows path `X:\...` ↔ device `/mnt/ext1/...`.

## Build

Docker Desktop required. Image: `larento/pocketbook-sdk:5.19-a13`.

Full apps:

```bat
scripts\build.bat
```

Reader-only relink (faster after C changes):

```powershell
$env:NASKILLA_GIT_SHA = (git rev-parse --short HEAD)
docker run --rm -e "NASKILLA_GIT_SHA=$env:NASKILLA_GIT_SHA" -v "${PWD}:/src" -w /src --entrypoint /bin/bash larento/pocketbook-sdk:5.19-a13 /src/scripts/link_reader.sh
```

Outputs under `build\`: `NaSkilla.app`, `Reader.app`, plus other apps from full build.

## Install (copy)

**All apps + eject:** `scripts\install.bat` (always ejects at end).

**Agent / debug copy (no automatic eject):**

```powershell
$pb = (Get-PSDrive -PSProvider FileSystem | Where-Object { Test-Path "$($_.Name):\system\config" } | Select-Object -First 1).Name + ':'
Copy-Item -Force build\NaSkilla.app "$pb\applications\NaSkilla Reader.app"
Copy-Item -Force build\Reader.app "$pb\applications\Reader.app"
# optional others: Vault.app, AISearch.app, Chef.app → applications\
```

Must land as **`NaSkilla Reader.app`** (space), not `NaSkilla.app`.

## Eject policy

| Situation | Action |
|-----------|--------|
| Iterative debug, more copies soon | Leave mounted |
| User will open app / check UI now | Eject |
| User says rozłącz / eject / disconnect | Eject |
| Editing logs or `debug.cfg` over USB | Leave mounted |

Eject:

```powershell
& .\scripts\eject_usb.ps1 -DriveLetter 'D:'   # use detected letter
```

Fallback if script fails:

```powershell
$drive = 'D:'; $shell = New-Object -ComObject Shell.Application
$shell.Namespace(17).ParseName("$drive\").InvokeVerb('Eject')
```

Success: `Test-Path "$drive\system\config"` is false.

## Key device paths

| Item | USB path |
|------|----------|
| Apps | `X:\applications\` |
| Settings | `X:\.naskilla.cfg` |
| Debug flags | `X:\system\state\naskilla\debug.cfg` |
| Draw safe sticky | `X:\system\state\naskilla\draw_safe_text` |
| Draw sentinel | `X:\system\state\naskilla\draw_inflight` |
| Log | `X:\system\state\naskilla.log` |
| Boot log | `X:\naskilla_boot.log` |

`debug.cfg` is not rewritten by the app. Typical keys: `render=text|auto|pattern`, `draw_probe=0..6`, `crash_test=0|1`. Delete `draw_safe_text` to allow Draw again after a crash sticky.

## After eject (user checklist)

1. Unplug or leave cable; device leaves USB Drive mode.
2. Open **Applications → NaSkilla Reader**.
3. For fonts/logo from full install: restart / Settings as needed.

More engine detail: `apps/Reader/ENGINE.md`.
