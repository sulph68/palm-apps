#!/bin/bash
# build_drvswap.sh - DrvSwap.prc (one segment, no icon: it is launched from the launcher list by name)
set -e
cd "$(dirname "$0")"
SDK=$HOME/palmos_sdk/palm-os-sdk/sdk-4/include
INCS=$(find "$SDK" -type d | sed 's/^/-I/' | tr '\n' ' ')
B=build_drvswap
mkdir -p "$B/res"
m68k-palmos-gcc -palmos4 -Os $INCS -I. -c DrvSwap.c -o "$B/DrvSwap.o"
m68k-palmos-gcc -palmos4 "$B/DrvSwap.o" -o "$B/DrvSwap"
pilrc DrvSwap.rcp "$B/res" >/dev/null 2>&1
printf 'DrvSwap\0' > "$B/res/tAIN03e8.bin"
printf '1.0\0'     > "$B/res/tver0001.bin"
build-prc -o DrvSwap.prc DrvSwap.def "$B/DrvSwap" "$B"/res/*.bin
echo "DONE: $(ls -la DrvSwap.prc | awk '{print $5}') bytes"
