#!/bin/bash
# Builds ExgTest.prc: Bkp1's ExgDB.c import/export driven through a memory buffer (no card
# needed), for POSE. usage: ./build.sh [dir with ExgDB.c, default ../Src] [out.prc]
set -e; cd "$(dirname "$0")"
SRC=${1:-../Src}; OUT=${2:-ExgTest.prc}
SDK=/home/cnrai/palmos_sdk/palm-os-sdk/sdk-4
INCS="$(find $SDK/include -type d | sed 's/^/-I/' | tr '\n' ' ') -I../compat -I$SRC"
mkdir -p build; rm -f build/*
m68k-palmos-gcc -palmos4 -Os $INCS -c exgtest.c -o build/exgtest.o 2>/dev/null
m68k-palmos-gcc -palmos4 -Os $INCS -c $SRC/ExgDB.c -o build/ExgDB.o 2>/dev/null
m68k-palmos-gcc -palmos4 build/exgtest.o build/ExgDB.o -o build/exgtest
pilrc -q exgtest.rcp build >/dev/null
build-prc -o $OUT exgtest.def build/exgtest build/*.bin
ls -la $OUT
