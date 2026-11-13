#!/bin/bash
set -e
export PATH=/sdk/usr/bin:/usr/bin:/bin
BIN=build/NaSkilla.debug
PC=0xc1af0
LR=0xa6c24bec

echo '=== ELF header ==='
arm-obreey-linux-gnueabi-readelf -h "$BIN" | egrep "Type:|Entry|Machine|Class|Flags"

echo '=== Program headers ==='
arm-obreey-linux-gnueabi-readelf -W -l "$BIN" | head -n 40

echo '=== Raw addr2line ==='
arm-obreey-linux-gnueabi-addr2line -e "$BIN" -f -C -p -a "$PC" "$LR"

echo '=== LR minus candidate bases ==='
for base in 0x0 0x8000 0x10000 0xa0000000 0xa6000000 0xa6c00000 0xa6c20000 0xb0000000; do
  off=$(printf "0x%x" $((LR - base)))
  echo -n "base=$base off=$off -> "
  arm-obreey-linux-gnueabi-addr2line -e "$BIN" -f -C -p -a "$off"
done

echo '=== Symbols ==='
arm-obreey-linux-gnueabi-nm "$BIN" | egrep "cre_draw|LVDocView" | head -n 40

echo '=== Size ==='
arm-obreey-linux-gnueabi-size "$BIN"
