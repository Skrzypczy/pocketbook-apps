@echo off
echo Building Vault.app...
docker run --rm -v "%cd%:/src" -w /src --entrypoint sh ^
  larento/pocketbook-sdk:5.19-a13 ^
  -c "export LD_LIBRARY_PATH=/sdk/usr/lib && /sdk/usr/bin/arm-obreey-linux-gnueabi-gcc -I/sdk/usr/arm-obreey-linux-gnueabi/sysroot/usr/include/freetype2 Vault.c -o Vault.app -linkview"

echo Building AISearch.app...
docker run --rm -v "%cd%:/src" -w /src --entrypoint sh ^
  larento/pocketbook-sdk:5.19-a13 ^
  -c "export LD_LIBRARY_PATH=/sdk/usr/lib && /sdk/usr/bin/arm-obreey-linux-gnueabi-gcc -I/sdk/usr/arm-obreey-linux-gnueabi/sysroot/usr/include/freetype2 AISearch.c -o AISearch.app -linkview"

echo Build complete.
