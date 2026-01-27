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
echo.

REM Initialize counters
set SUCCESS=0
set FAILED=0
set SKIPPED=0

REM Create directories if needed
if not exist "%POCKETBOOK_DRIVE%\applications" mkdir "%POCKETBOOK_DRIVE%\applications"
if not exist "%POCKETBOOK_DRIVE%\Private" mkdir "%POCKETBOOK_DRIVE%\Private"

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
if exist "config\.ai_api_key" (
    copy /Y "config\.ai_api_key" "%POCKETBOOK_DRIVE%\" >nul 2>&1
    if errorlevel 1 (
        echo   FAILED - Could not copy API key
        set /a FAILED+=1
    ) else (
        echo   OK (used by AISearch and Chef)
        set /a SUCCESS+=1
    )
) else (
    echo   SKIP - config\.ai_api_key not found
    set /a SKIPPED+=1
)

REM Install Amazon Kindle font (Bookerly)
echo Installing Bookerly font...
if not exist "%POCKETBOOK_DRIVE%\fonts" mkdir "%POCKETBOOK_DRIVE%\fonts"
if exist "config\fonts\Bookerly-Regular.ttf" (
    copy /Y "config\fonts\Bookerly*.ttf" "%POCKETBOOK_DRIVE%\fonts\" >nul 2>&1
    if errorlevel 1 (
        echo   FAILED - Could not copy font files
        set /a FAILED+=1
    ) else (
        echo   OK
        set /a SUCCESS+=1
    )
) else (
    echo   SKIP - Run scripts\build.bat first to download fonts
    set /a SKIPPED+=1
)

REM Install screensaver images
echo Installing screensaver...
if not exist "%POCKETBOOK_DRIVE%\system\logo" mkdir "%POCKETBOOK_DRIVE%\system\logo"
if exist "config\ScreenSaver\Nature.png" (
    copy /Y "config\ScreenSaver\Nature.png" "%POCKETBOOK_DRIVE%\system\logo\poweroff.png" >nul 2>&1
    if errorlevel 1 (
        echo   FAILED - Could not copy screensaver
        set /a FAILED+=1
    ) else (
        echo   OK (Nature screensaver)
        set /a SUCCESS+=1
    )
) else (
    echo   SKIP - config\ScreenSaver\Nature.png not found
    set /a SKIPPED+=1
)

echo.
echo ==========================================
echo   Installation Summary
echo ==========================================
echo   Success: %SUCCESS%  Failed: %FAILED%  Skipped: %SKIPPED%
echo.
echo Installed to %POCKETBOOK_DRIVE%:
echo   - applications\Vault.app
echo   - applications\AISearch.app
echo   - applications\Chef.app
echo   - .ai_api_key (for AISearch)
echo   - Private\ (folder for Vault)
echo   - fonts\Bookerly*.ttf (Amazon Kindle font)
echo   - system\logo\poweroff.png (screensaver)
echo.
echo Next steps:
echo   1. Create .chef_api_key file on device for Chef app
echo   2. Safely eject PocketBook
echo   3. Apps will appear in Applications menu
echo   4. Select Bookerly font in reader settings
echo   5. Screensaver shows on power off
echo.
popd
pause
