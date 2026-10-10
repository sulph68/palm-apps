#!/bin/bash
# build_sectest.sh - SecTest: verifies enccore on-device. Single segment.
set -e
cd "$(dirname "$0")"
SDK=/home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/include
GLUE=/home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/lib/m68k-palmos-coff
INCS=$(find "$SDK" -type d | sed 's/^/-I/' | tr '\n' ' ')
CFLAGS="-palmos4 -O2 $INCS -I."
CC=m68k-palmos-gcc
B=build_sectest
mkdir -p "$B" "$B/res"

for f in sectest enccore aes; do
  $CC $CFLAGS -c "$f.c" -o "$B/$f.o"
done
$CC $CFLAGS "$B"/*.o -L"$GLUE" -lPalmOSGlue -o "$B/SecTest"

pilrc sectest.rcp "$B/res" >/dev/null 2>&1
build-prc -o SecTest.prc SecTest.def "$B/SecTest" "$B"/res/*.bin
echo "DONE: $(ls -la SecTest.prc | awk '{print $5}') bytes"
