@echo off
REM PocketBook Apps Installer
REM Run from repository root: scripts\install.bat

REM Get script directory and repo root
set SCRIPT_DIR=%~dp0
set REPO_ROOT=%SCRIPT_DIR%..

pushd "%REPO_ROOT%"

echo ==========================================
echo   PocketBook Apps Installer
echo ==========================================
echo.

REM Check if build folder exists
if not exist "build\Vault.app" (
    echo ERROR: Apps not built yet!
    echo Run scripts\build.bat first.
    echo.
    pause
    popd
    exit /b 1
)

REM Check if PocketBook is connected
set POCKETBOOK_DRIVE=
for %%d in (D E F G H I J K L M N O P Q R S T U V W X Y Z) do (
    if exist "%%d:\system\config" (
        set POCKETBOOK_DRIVE=%%d:
        goto :found
    )
)

echo ERROR: PocketBook not found!
echo.
echo Please:
echo   1. Connect your PocketBook via USB
echo   2. Select "USB Drive" mode on the device
echo   3. Run this script again
echo.
pause
popd
exit /b 1

:found
echo Found PocketBook at %POCKETBOOK_DRIVE%

REM Get volume name for later ejection
for /f "tokens=*" %%v in ('wmic logicaldisk where "DeviceID='%POCKETBOOK_DRIVE%'" get VolumeName /value 2^>nul ^| find "="') do set %%v
echo Volume: %VolumeName%
echo.

REM Initialize counters
set SUCCESS=0
set FAILED=0
set SKIPPED=0

REM Create directories if needed
if not exist "%POCKETBOOK_DRIVE%\applications" mkdir "%POCKETBOOK_DRIVE%\applications"


REM Note: Don't create Private folder - Vault app manages it

REM Copy apps from build folder
echo Installing Vault.app...
copy /Y "build\Vault.app" "%POCKETBOOK_DRIVE%\applications\" >nul 2>&1
if errorlevel 1 (
    echo   FAILED - Make sure build\Vault.app exists
    set /a FAILED+=1
) else (
    echo   OK
    set /a SUCCESS+=1
)

echo Installing AISearch.app...
copy /Y "build\AISearch.app" "%POCKETBOOK_DRIVE%\applications\" >nul 2>&1
if errorlevel 1 (
    echo   FAILED - Make sure build\AISearch.app exists
    set /a FAILED+=1
) else (
    echo   OK
    set /a SUCCESS+=1
)

echo Installing Chef.app...
copy /Y "build\Chef.app" "%POCKETBOOK_DRIVE%\applications\" >nul 2>&1
if errorlevel 1 (
    echo   FAILED - Make sure build\Chef.app exists
    set /a FAILED+=1
) else (
    echo   OK
    set /a SUCCESS+=1
)

REM Copy API key for AI apps from config folder
echo Installing API key...
if not exist "config\.ai_api_key" (
    echo   SKIP - config\.ai_api_key not found
    set /a SKIPPED+=1
    goto :after_apikey
)
copy /Y "config\.ai_api_key" "%POCKETBOOK_DRIVE%\" >nul 2>&1
if errorlevel 1 (
    echo   FAILED - Could not copy API key
    set /a FAILED+=1
) else (
    echo   OK (used by AISearch and Chef)
    set /a SUCCESS+=1
)
:after_apikey

REM Install Amazon Kindle font (Bookerly)
echo Installing Bookerly font...
if not exist "%POCKETBOOK_DRIVE%\system\fonts" mkdir "%POCKETBOOK_DRIVE%\system\fonts"
if not exist "config\fonts\Bookerly-Regular.ttf" (
    echo   SKIP - Run scripts\build.bat first to download fonts
    set /a SKIPPED+=1
    goto :after_fonts
)
copy /Y "config\fonts\Bookerly*.ttf" "%POCKETBOOK_DRIVE%\system\fonts\" >nul 2>&1
if errorlevel 1 (
    echo   FAILED - Could not copy font files
    set /a FAILED+=1
) else (
    echo   OK
    set /a SUCCESS+=1
)
:after_fonts

REM Install screensaver images
echo Installing screensaver...
if not exist "%POCKETBOOK_DRIVE%\system\logo\offlogo" mkdir "%POCKETBOOK_DRIVE%\system\logo\offlogo"
if not exist "config\ScreenSaver\Nature.png" (
    echo   SKIP - config\ScreenSaver\Nature.png not found
    set /a SKIPPED+=1
    goto :after_screensaver
)
copy /Y "config\ScreenSaver\Nature.png" "%POCKETBOOK_DRIVE%\system\logo\offlogo\Nature.png" >nul 2>&1
if errorlevel 1 (
    echo   FAILED - Could not copy screensaver
    set /a FAILED+=1
) else (
    echo   OK (select in Settings ^> Personalize ^> Logo)
    set /a SUCCESS+=1
)
:after_screensaver

echo.

echo Next steps:
echo ==========================================
echo   Installation Summary
echo ==========================================
echo   Success: %SUCCESS%  Failed: %FAILED%  Skipped: %SKIPPED%
echo.
echo Installed to %POCKETBOOK_DRIVE%
echo   - applications\Vault.app
echo   - applications\AISearch.app
echo   - applications\Chef.app
echo   - .ai_api_key (for AISearch and Chef)
echo   - system\fonts\Bookerly*.ttf (Amazon Kindle font)
echo   - system\logo\offlogo\Nature.png (power-off logo)
echo.

REM === Eject PocketBook device after install ===
echo Attempting to safely eject PocketBook device...

REM Flush file buffers and give OS a moment to release handles
powershell -Command "[System.IO.DriveInfo]::GetDrives() | Out-Null; Start-Sleep -Milliseconds 500" 2>nul

REM Call eject script for the specific drive (pass 'D:' format)
PowerShell -ExecutionPolicy Bypass -File "%SCRIPT_DIR%eject_usb.ps1" -DriveLetter "%POCKETBOOK_DRIVE%" 
set "EJECT_EXIT=%ERRORLEVEL%"
if "%EJECT_EXIT%"=="0" (
    echo   OK - PocketBook safely ejected!
) else (
    echo   WARNING: Auto-eject failed (exit code %EJECT_EXIT%). Please use "Safely Remove Hardware" icon in taskbar.
)
echo.
echo   1. Apps will appear in Applications menu
echo   2. Restart device to detect new fonts
echo   3. Select Bookerly font in: Settings ^> Fonts and Rendering
echo   4. SET LOGO: Settings ^> Personalize ^> Power-off logo ^> Nature.png
echo.
popd
pause
