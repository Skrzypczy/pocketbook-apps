#!/bin/bash
export PATH=/sdk/usr/bin:/usr/bin:/bin
BIN=build/NaSkilla.debug
arm-obreey-linux-gnueabi-nm "$BIN" | grep getGlyph
arm-obreey-linux-gnueabi-objdump -d --start-address=0xc1a80 --stop-address=0xc1b80 "$BIN"
arm-obreey-linux-gnueabi-addr2line -e "$BIN" -f -C -p -a 0xc1af0 0xc1ae0 0xc1b00
