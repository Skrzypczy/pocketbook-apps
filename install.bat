@echo off
echo ==========================================
echo   PocketBook Apps Installer
echo ==========================================
echo.

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
exit /b 1

:found
echo Found PocketBook at %POCKETBOOK_DRIVE%
echo.

REM Create directories if needed
if not exist "%POCKETBOOK_DRIVE%\applications" mkdir "%POCKETBOOK_DRIVE%\applications"
if not exist "%POCKETBOOK_DRIVE%\Private" mkdir "%POCKETBOOK_DRIVE%\Private"

REM Copy apps
echo Installing Vault.app...
copy /Y "Vault.app" "%POCKETBOOK_DRIVE%\applications\" >nul
if errorlevel 1 (
    echo   FAILED - Make sure Vault.app exists
) else (
    echo   OK
)

echo Installing AISearch.app...
copy /Y "AISearch.app" "%POCKETBOOK_DRIVE%\applications\" >nul
if errorlevel 1 (
    echo   FAILED - Make sure AISearch.app exists
) else (
    echo   OK
)

REM Copy API key for AISearch
echo Installing API key...
copy /Y ".ai_api_key" "%POCKETBOOK_DRIVE%\" >nul
if errorlevel 1 (
    echo   FAILED - Make sure .ai_api_key exists
) else (
    echo   OK
)

echo.
echo ==========================================
echo   Installation Complete!
echo ==========================================
echo.
echo Installed to %POCKETBOOK_DRIVE%:
echo   - applications\Vault.app
echo   - applications\AISearch.app
echo   - .ai_api_key
echo   - Private\ (folder for Vault)
echo.
echo Next steps:
echo   1. Safely eject PocketBook
echo   2. Apps will appear in Applications menu
echo   3. For AISearch: export Calibre library to
echo      "My books.xml" and copy to device root
echo.
pause
