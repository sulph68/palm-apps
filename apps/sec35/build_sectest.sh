#!/bin/bash
# build_sectest.sh - SecTest: verifies enccore on-device. Single segment.
set -e
cd "$(dirname "$0")"
SDK=$HOME/palmos_sdk/palm-os-sdk/sdk-4/include
GLUE=$HOME/palmos_sdk/palm-os-sdk/sdk-4/lib/m68k-palmos-coff
INCS=$(find "$SDK" -type d | sed 's/^/-I/' | tr '\n' ' ')
CFLAGS="-palmos4 -O2 $INCS -I. -I../lib"
CC=m68k-palmos-gcc
B=build_sectest
mkdir -p "$B" "$B/res"

$CC $CFLAGS -c sectest.c -o "$B/sectest.o"
for f in enccore aes; do
  $CC $CFLAGS -c "../lib/$f.c" -o "$B/$f.o"
done
$CC $CFLAGS "$B"/*.o -L"$GLUE" -lPalmOSGlue -o "$B/SecTest"

pilrc sectest.rcp "$B/res" >/dev/null 2>&1
build-prc -o SecTest.prc SecTest.def "$B/SecTest" "$B"/res/*.bin
echo "DONE: $(ls -la SecTest.prc | awk '{print $5}') bytes"
