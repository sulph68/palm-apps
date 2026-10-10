#!/bin/bash
# build_sec35.sh - Sec35 security manager. Single segment.
set -e
cd "$(dirname "$0")"
SDK=/home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/include
GLUE=/home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/lib/m68k-palmos-coff
INCS=$(find "$SDK" -type d | sed 's/^/-I/' | tr '\n' ' ')
CFLAGS="-palmos4 -Os $INCS -I."
CC=m68k-palmos-gcc
B=build_sec35
mkdir -p "$B" "$B/res"

for f in sec35 enccore aes; do
  $CC $CFLAGS -c "$f.c" -o "$B/$f.o"
done
$CC $CFLAGS "$B"/*.o -L"$GLUE" -lPalmOSGlue -o "$B/Sec35"

pilrc sec35.rcp "$B/res" >/dev/null 2>&1

# Launcher icon = padlock disc + "35" overlay, in the "35"-suite style. The
# launcher reads the standard 'tAIB' app icon (1000 large / 1001 small); emit it
# here from the committed BMPs (regenerate with: python3 mkicon_sec35.py).
cp icons/icon35_large.bmp "$B/big.bmp"; cp icons/icon35_small.bmp "$B/sml.bmp"
( cd "$B"; printf 'ICON "big.bmp"\nSMALLICON "sml.bmp"\n' > appicon.rcp; \
  pilrc appicon.rcp res ) >/dev/null 2>&1
rm -f "$B/res/taif"*.bin "$B/res/taic"*.bin        # drop any stale editor icons

printf 'Sec35\0' > "$B/res/tAIN03e8.bin"           # launcher / app name
printf '1.0\0'   > "$B/res/tver0001.bin"           # version 1.0

build-prc -o Sec35.prc Sec35.def "$B/Sec35" "$B"/res/*.bin
echo "DONE: $(ls -la Sec35.prc | awk '{print $5}') bytes"
