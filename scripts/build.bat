@echo off
REM PocketBook Apps Build Script
REM Run from repository root: scripts\build.bat

REM Get script directory and repo root
set SCRIPT_DIR=%~dp0
set REPO_ROOT=%SCRIPT_DIR%..

pushd "%REPO_ROOT%"

echo ==========================================
echo   PocketBook Apps Builder
echo ==========================================
echo.

REM Create build output directory
if not exist "build" mkdir build

echo Building Vault.app...
docker run --rm -v "%cd%:/src" -w /src --entrypoint sh ^
  larento/pocketbook-sdk:5.19-a13 ^
  -c "export LD_LIBRARY_PATH=/sdk/usr/lib && /sdk/usr/bin/arm-obreey-linux-gnueabi-gcc -I/sdk/usr/arm-obreey-linux-gnueabi/sysroot/usr/include/freetype2 apps/Vault/Vault.c -o build/Vault.app -linkview"
if errorlevel 1 (
    echo   FAILED
) else (
    echo   OK
)

echo Building AISearch.app...
docker run --rm -v "%cd%:/src" -w /src --entrypoint sh ^
  larento/pocketbook-sdk:5.19-a13 ^
  -c "export LD_LIBRARY_PATH=/sdk/usr/lib && /sdk/usr/bin/arm-obreey-linux-gnueabi-gcc -I/sdk/usr/arm-obreey-linux-gnueabi/sysroot/usr/include/freetype2 apps/AISearch/AISearch.c -o build/AISearch.app -linkview -lsqlite3"
if errorlevel 1 (
    echo   FAILED
) else (
    echo   OK
)

echo Building Chef.app...
docker run --rm -v "%cd%:/src" -w /src --entrypoint sh ^
  larento/pocketbook-sdk:5.19-a13 ^
  -c "export LD_LIBRARY_PATH=/sdk/usr/lib && /sdk/usr/bin/arm-obreey-linux-gnueabi-gcc -I/sdk/usr/arm-obreey-linux-gnueabi/sysroot/usr/include/freetype2 apps/Chef/Chef.c -o build/Chef.app -linkview"
if errorlevel 1 (
    echo   FAILED
) else (
    echo   OK
)

REM Download Bookerly fonts if not already cached
echo.
echo Downloading Bookerly fonts...
if not exist "config\fonts" mkdir "config\fonts"

set FONT_BASE_URL=https://raw.githubusercontent.com/RadicalMilitantLibrary/www/master/fonts
set NEED_DOWNLOAD=0

if not exist "config\fonts\Bookerly-Regular.ttf" set NEED_DOWNLOAD=1
if not exist "config\fonts\Bookerly-Bold.ttf" set NEED_DOWNLOAD=1
if not exist "config\fonts\Bookerly-Italic.ttf" set NEED_DOWNLOAD=1

if %NEED_DOWNLOAD%==1 (
    curl -sL "%FONT_BASE_URL%/Bookerly-Regular.ttf" -o "config\fonts\Bookerly-Regular.ttf"
    curl -sL "%FONT_BASE_URL%/Bookerly-Bold.ttf" -o "config\fonts\Bookerly-Bold.ttf"
    curl -sL "%FONT_BASE_URL%/Bookerly-Italic.ttf" -o "config\fonts\Bookerly-Italic.ttf"
    if exist "config\fonts\Bookerly-Regular.ttf" (
        echo   OK
    ) else (
        echo   FAILED - check internet connection
    )
) else (
    echo   OK (cached)
)

echo.
echo ==========================================
echo   Build Output: build\
echo ==========================================
dir /b build\*.app 2>nul
echo.
echo Fonts: config\fonts\
dir /b config\fonts\*.ttf 2>nul

popd
