#!/bin/bash
# build_eraNTP.sh - EraNTP. Single segment, modeled on sec35/build_sec35.sh.
set -e
cd "$(dirname "$0")"
SDK=/home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/include
GLUE=/home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/lib/m68k-palmos-coff
INCS=$(find "$SDK" -type d | sed 's/^/-I/' | tr '\n' ' ')
CFLAGS="-palmos4 -Os $INCS -I. -I../lib"
CC=m68k-palmos-gcc
B=build_eraNTP
mkdir -p "$B" "$B/res"

for f in EraNTP:. datelib:../lib hackcheck:../lib; do
  name="${f%%:*}"; dir="${f##*:}"
  $CC $CFLAGS -c "$dir/$name.c" -o "$B/$name.o"
done
$CC $CFLAGS "$B"/*.o -L"$GLUE" -lPalmOSGlue -o "$B/EraNTP"

pilrc EraNTP.rcp "$B/res" >/dev/null 2>&1

# Launcher icon = network clock (see icons/mkicon_eranTP.py). The launcher
# reads the standard 'tAIB' app icon (1000 large / 1001 small); emit it here
# from the committed BMPs (regenerate with: python3 mkicon_eranTP.py).
cp icons/eranTP_large.bmp "$B/big.bmp"; cp icons/eranTP_small.bmp "$B/sml.bmp"
( cd "$B"; printf 'ICON "big.bmp"\nSMALLICON "sml.bmp"\n' > appicon.rcp; \
  pilrc appicon.rcp res ) >/dev/null 2>&1
rm -f "$B/res/taif"*.bin "$B/res/taic"*.bin        # drop any stale editor icons

printf 'EraNTP\0' > "$B/res/tAIN03e8.bin"          # launcher / app name
printf '1.0\0'    > "$B/res/tver0001.bin"          # version 1.0

build-prc -o EraNTP.prc EraNTP.def "$B/EraNTP" "$B"/res/*.bin
echo "DONE: $(ls -la EraNTP.prc | awk '{print $5}') bytes"
